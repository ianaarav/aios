/*
 * AIOS 0.4.0 - a modern take on MS-DOS.
 * Bare-metal x86_64 kernel booted by Limine (works on legacy BIOS).
 *
 * Everything here talks to the hardware directly:
 *   - framebuffer text console (built-in 8x8 font, scaled)
 *   - PS/2 keyboard (polled)
 *   - CMOS real-time clock
 *   - read-only RAM disk (a TAR archive loaded by Limine as a module)
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <limine.h>

#include "font.h"

#define AIOS_VERSION  "0.4.0"
#define INPUT_SIZE    128
#define HISTORY_MAX   16
#define FONT_SCALE    2

/* ------------------------------------------------------------------ */
/* Limine requests                                                     */
/* ------------------------------------------------------------------ */

__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(2);

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER

__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER

/* ------------------------------------------------------------------ */
/* Freestanding libc bits the compiler may call                        */
/* ------------------------------------------------------------------ */

void *memcpy(void *dest, const void *src, size_t n)
{
    void *ret = dest;
    __asm__ volatile ("rep movsb" : "+D"(dest), "+S"(src), "+c"(n) : : "memory");
    return ret;
}

void *memset(void *dest, int c, size_t n)
{
    void *ret = dest;
    __asm__ volatile ("rep stosb" : "+D"(dest), "+c"(n) : "a"(c) : "memory");
    return ret;
}

void *memmove(void *dest, const void *src, size_t n)
{
    void *ret = dest;
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    if (n == 0 || d == s) {
        return ret;
    }

    if (d < s || d >= s + n) {
        __asm__ volatile ("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    } else {
        d += n - 1;
        s += n - 1;
        __asm__ volatile ("std\n\trep movsb\n\tcld" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    }
    return ret;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *p = (const uint8_t *)a;
    const uint8_t *q = (const uint8_t *)b;

    for (size_t i = 0; i < n; i++) {
        if (p[i] != q[i]) {
            return p[i] < q[i] ? -1 : 1;
        }
    }
    return 0;
}

int bcmp(const void *a, const void *b, size_t n)
{
    return memcmp(a, b, n);
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static inline void outb(uint16_t port, uint8_t v)
{
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outw(uint16_t port, uint16_t v)
{
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(port));
}

static void hcf(void)
{
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static size_t k_strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static char k_up(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static int k_stricmp(const char *a, const char *b)
{
    while (*a && *b) {
        char x = k_up(*a);
        char y = k_up(*b);
        if (x != y) {
            return x < y ? -1 : 1;
        }
        a++;
        b++;
    }
    if (*a == *b) {
        return 0;
    }
    return *a ? 1 : -1;
}

static int k_strnicmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char x = k_up(a[i]);
        char y = k_up(b[i]);
        if (x != y) {
            return x < y ? -1 : 1;
        }
        if (a[i] == 0) {
            return 0;
        }
    }
    return 0;
}

static void k_strlcpy(char *dst, const char *src, size_t size)
{
    size_t i = 0;
    if (size == 0) {
        return;
    }
    while (src[i] && i + 1 < size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void k_strlcat(char *dst, const char *src, size_t size)
{
    size_t n = k_strlen(dst);
    if (n + 1 >= size) {
        return;
    }
    k_strlcpy(dst + n, src, size - n);
}

/* ------------------------------------------------------------------ */
/* Framebuffer text console                                            */
/* ------------------------------------------------------------------ */

static const uint32_t palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static uint8_t  *fb_base;
static uint64_t fb_w, fb_h, fb_pitch;
static uint8_t  r_shift, g_shift, b_shift;
static uint32_t cell_w, cell_h, margin_x, margin_y;
static uint32_t cols, rows;
static uint32_t cx, cy;
static uint8_t  cur_fg = 7, cur_bg = 0;
static bool     cursor_on = false;

static uint32_t pack_rgb(uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 0xFF;
    uint32_t g = (rgb >> 8) & 0xFF;
    uint32_t b = rgb & 0xFF;
    return (r << r_shift) | (g << g_shift) | (b << b_shift);
}

static void fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color)
{
    for (uint64_t j = 0; j < h; j++) {
        uint32_t *line = (uint32_t *)(fb_base + (y + j) * fb_pitch) + x;
        for (uint64_t i = 0; i < w; i++) {
            line[i] = color;
        }
    }
}

static void draw_glyph(uint32_t col, uint32_t row, unsigned char ch)
{
    const uint8_t *g;
    uint32_t fg = pack_rgb(palette[cur_fg]);
    uint32_t bg = pack_rgb(palette[cur_bg]);
    uint64_t x0 = margin_x + (uint64_t)col * cell_w;
    uint64_t y0 = margin_y + (uint64_t)row * cell_h;

    if (ch >= 32 && ch <= 126) {
        g = font8x8[ch - 32];
    } else {
        g = font8x8['?' - 32];
    }

    for (uint32_t gy = 0; gy < 8; gy++) {
        uint8_t bits = g[gy];
        for (uint32_t sy = 0; sy < FONT_SCALE; sy++) {
            uint32_t *line = (uint32_t *)(fb_base + (y0 + gy * FONT_SCALE + sy) * fb_pitch) + x0;
            for (uint32_t gx = 0; gx < 8; gx++) {
                uint32_t px = ((bits >> gx) & 1) ? fg : bg;
                for (uint32_t sx = 0; sx < FONT_SCALE; sx++) {
                    line[gx * FONT_SCALE + sx] = px;
                }
            }
        }
    }
}

static void invert_cell(uint32_t col, uint32_t row)
{
    uint32_t mask = pack_rgb(0xFFFFFF);
    uint64_t x0 = margin_x + (uint64_t)col * cell_w;
    uint64_t y0 = margin_y + (uint64_t)row * cell_h;

    for (uint32_t j = 0; j < cell_h; j++) {
        uint32_t *line = (uint32_t *)(fb_base + (y0 + j) * fb_pitch) + x0;
        for (uint32_t i = 0; i < cell_w; i++) {
            line[i] ^= mask;
        }
    }
}

static void cursor_hide(void)
{
    if (cursor_on) {
        if (cx < cols && cy < rows) {
            invert_cell(cx, cy);
        }
        cursor_on = false;
    }
}

static void cursor_show(void)
{
    if (!cursor_on) {
        if (cx < cols && cy < rows) {
            invert_cell(cx, cy);
        }
        cursor_on = true;
    }
}

static void scroll_up(void)
{
    uint64_t text_top = margin_y;
    uint64_t move_bytes = (uint64_t)(rows - 1) * cell_h * fb_pitch;

    memmove(fb_base + text_top * fb_pitch,
            fb_base + (text_top + cell_h) * fb_pitch,
            move_bytes);

    fill_rect(0, text_top + (uint64_t)(rows - 1) * cell_h, fb_w, cell_h,
              pack_rgb(palette[cur_bg]));
}

static void putc_raw(char c)
{
    switch (c) {
        case '\n':
            cx = 0;
            cy++;
            break;
        case '\r':
            cx = 0;
            break;
        case '\b':
            if (cx > 0) {
                cx--;
            }
            break;
        case '\t':
            cx = (cx + 8) & ~7u;
            if (cx >= cols) {
                cx = 0;
                cy++;
            }
            break;
        default:
            draw_glyph(cx, cy, (unsigned char)c);
            cx++;
            if (cx >= cols) {
                cx = 0;
                cy++;
            }
            break;
    }

    if (cy >= rows) {
        scroll_up();
        cy = rows - 1;
    }
}

static void con_write(const char *s)
{
    cursor_hide();
    while (*s) {
        putc_raw(*s++);
    }
    cursor_show();
}

static void con_putc(char c)
{
    cursor_hide();
    putc_raw(c);
    cursor_show();
}


static void con_set_color(uint8_t fg, uint8_t bg)
{
    cur_fg = fg & 0xF;
    cur_bg = bg & 0xF;
}

static void con_clear(void)
{
    cursor_on = false;
    fill_rect(0, 0, fb_w, fb_h, pack_rgb(palette[cur_bg]));
    cx = 0;
    cy = 0;
    cursor_show();
}

static bool con_init(struct limine_framebuffer *fb)
{
    if (fb->bpp != 32) {
        return false;
    }

    fb_base  = (uint8_t *)fb->address;
    fb_w     = fb->width;
    fb_h     = fb->height;
    fb_pitch = fb->pitch;
    r_shift  = fb->red_mask_shift;
    g_shift  = fb->green_mask_shift;
    b_shift  = fb->blue_mask_shift;

    cell_w = 8 * FONT_SCALE;
    cell_h = 8 * FONT_SCALE;
    cols = (uint32_t)(fb_w / cell_w);
    rows = (uint32_t)(fb_h / cell_h);
    margin_x = (uint32_t)((fb_w - (uint64_t)cols * cell_w) / 2);
    margin_y = (uint32_t)((fb_h - (uint64_t)rows * cell_h) / 2);

    con_clear();
    return true;
}

/* ------------------------------------------------------------------ */
/* printf                                                              */
/* ------------------------------------------------------------------ */

static size_t k_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    size_t n = 0;

#define PUT(ch) do { if (n + 1 < size) { buf[n] = (ch); } n++; } while (0)

    for (; *fmt; fmt++) {
        bool left = false, zero = false, sign = false;
        int width = 0, lng = 0, pad, blen = 0;
        char tmp[32];
        const char *body = tmp;

        if (*fmt != '%') {
            PUT(*fmt);
            continue;
        }
        fmt++;

        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') {
                left = true;
            } else {
                zero = true;
            }
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        while (*fmt == 'l') {
            lng++;
            fmt++;
        }

        switch (*fmt) {
            case 's':
                body = va_arg(ap, const char *);
                if (!body) {
                    body = "(null)";
                }
                blen = (int)k_strlen(body);
                zero = false;
                break;

            case 'c':
                tmp[0] = (char)va_arg(ap, int);
                blen = 1;
                zero = false;
                break;

            case 'd':
            case 'u':
            case 'x':
            case 'X': {
                uint64_t u;
                uint64_t base = (*fmt == 'x' || *fmt == 'X') ? 16 : 10;
                const char *digits = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                char rev[24];
                int rl = 0;

                if (*fmt == 'd') {
                    int64_t v = lng ? (int64_t)va_arg(ap, long) : (int64_t)va_arg(ap, int);
                    sign = v < 0;
                    u = sign ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
                } else {
                    u = lng ? (uint64_t)va_arg(ap, unsigned long)
                            : (uint64_t)va_arg(ap, unsigned int);
                }

                do {
                    rev[rl++] = digits[u % base];
                    u /= base;
                } while (u);

                while (rl > 0) {
                    tmp[blen++] = rev[--rl];
                }
                break;
            }

            case '%':
                tmp[0] = '%';
                blen = 1;
                zero = false;
                break;

            case 0:
                goto done;

            default:
                tmp[0] = '%';
                tmp[1] = *fmt;
                blen = 2;
                zero = false;
                break;
        }

        pad = width - blen - (sign ? 1 : 0);
        if (pad < 0) {
            pad = 0;
        }

        if (!left && !zero) {
            while (pad > 0) {
                PUT(' ');
                pad--;
            }
        }
        if (sign) {
            PUT('-');
        }
        if (!left && zero) {
            while (pad > 0) {
                PUT('0');
                pad--;
            }
        }
        for (int i = 0; i < blen; i++) {
            PUT(body[i]);
        }
        if (left) {
            while (pad > 0) {
                PUT(' ');
                pad--;
            }
        }
    }

done:
    if (size > 0) {
        buf[n < size - 1 ? n : size - 1] = 0;
    }
    return n;

#undef PUT
}

static void kprintf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    k_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    con_write(buf);
}

/* ------------------------------------------------------------------ */
/* PS/2 keyboard                                                       */
/* ------------------------------------------------------------------ */

enum {
    KEY_UP = 0x100, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME, KEY_END, KEY_DELETE
};

/* Scancode set 1, US layout. Index = scancode (0x00..0x39). */
static const char sc_lower[58] =
    "\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static const char sc_upper[58] =
    "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

static int kbd_poll(void)
{
    static bool shift = false, caps = false, ext = false;
    uint8_t status = inb(0x64);
    uint8_t sc;
    bool release, e;
    char c;

    if (!(status & 1)) {
        return -1;
    }

    sc = inb(0x60);

    if (status & 0x20) {        /* mouse byte, ignore */
        return -1;
    }
    if (sc == 0xE0) {
        ext = true;
        return -1;
    }
    if (sc == 0xE1) {           /* Pause/Break prefix, ignore */
        return -1;
    }

    release = (sc & 0x80) != 0;
    sc &= 0x7F;
    e = ext;
    ext = false;

    if (e) {
        if (release) {
            return -1;
        }
        switch (sc) {
            case 0x48: return KEY_UP;
            case 0x50: return KEY_DOWN;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
            case 0x47: return KEY_HOME;
            case 0x4F: return KEY_END;
            case 0x53: return KEY_DELETE;
            default:   return -1;
        }
    }

    if (sc == 0x2A || sc == 0x36) {
        shift = !release;
        return -1;
    }
    if (sc == 0x3A) {
        if (!release) {
            caps = !caps;
        }
        return -1;
    }
    if (release || sc >= sizeof(sc_lower)) {
        return -1;
    }

    c = sc_lower[sc];
    if (c >= 'a' && c <= 'z') {
        if (shift != caps) {
            c = sc_upper[sc];
        }
    } else if (shift) {
        c = sc_upper[sc];
    }

    if (c == 0) {
        return -1;
    }
    return (unsigned char)c;
}

static int kbd_read(void)
{
    int k;
    while ((k = kbd_poll()) < 0) {
        __asm__ volatile ("pause");
    }
    return k;
}

static void kbd_flush(void)
{
    int guard = 0;
    while ((inb(0x64) & 1) && guard++ < 64) {
        inb(0x60);
    }
}

/* ------------------------------------------------------------------ */
/* CMOS real-time clock, CPUID                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t year, month, day, hour, minute, second;
} rtc_time_t;

static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static uint32_t bcd(uint8_t v)
{
    return (uint32_t)((v & 0x0F) + (v >> 4) * 10);
}

static void rtc_get(rtc_time_t *t)
{
    uint8_t sec, min, hour, day, mon, year, sb;
    bool pm;

    while (cmos_read(0x0A) & 0x80) {
        /* wait for update to finish */
    }

    sec  = cmos_read(0x00);
    min  = cmos_read(0x02);
    hour = cmos_read(0x04);
    day  = cmos_read(0x07);
    mon  = cmos_read(0x08);
    year = cmos_read(0x09);
    sb   = cmos_read(0x0B);

    pm = (hour & 0x80) != 0;
    hour &= 0x7F;

    if (!(sb & 0x04)) {
        t->second = bcd(sec);
        t->minute = bcd(min);
        t->hour   = bcd(hour);
        t->day    = bcd(day);
        t->month  = bcd(mon);
        t->year   = 2000 + bcd(year);
    } else {
        t->second = sec;
        t->minute = min;
        t->hour   = hour;
        t->day    = day;
        t->month  = mon;
        t->year   = 2000u + year;
    }

    if (!(sb & 0x02)) {         /* 12-hour mode */
        if (pm && t->hour < 12) {
            t->hour += 12;
        } else if (!pm && t->hour == 12) {
            t->hour = 0;
        }
    }
}

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ volatile ("cpuid"
                      : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                      : "a"(leaf), "c"(0));
}

/* ------------------------------------------------------------------ */
/* RAM disk (TAR archive)                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char           path[128];   /* normalised: "dir/file", no leading or trailing slash */
    uint64_t       size;
    const uint8_t  *data;
    bool           is_dir;
} fsent_t;

static const uint8_t *ramdisk = NULL;
static uint64_t       ramdisk_size = 0;
static char           cwd[128] = "";

static uint64_t parse_octal(const char *s, int n)
{
    uint64_t v = 0;
    int i = 0;

    while (i < n && s[i] == ' ') {
        i++;
    }
    for (; i < n && s[i] >= '0' && s[i] <= '7'; i++) {
        v = v * 8 + (uint64_t)(s[i] - '0');
    }
    return v;
}

static void normalise_name(const char *in, char *out, size_t outsz)
{
    size_t len;

    while (in[0] == '.' && in[1] == '/') {
        in += 2;
    }
    while (in[0] == '/') {
        in++;
    }
    if (in[0] == '.' && in[1] == 0) {
        in++;
    }

    k_strlcpy(out, in, outsz);

    len = k_strlen(out);
    while (len > 0 && out[len - 1] == '/') {
        out[--len] = 0;
    }
}

static bool tar_next(uint64_t *off, fsent_t *e)
{
    while (*off + 512 <= ramdisk_size) {
        const uint8_t *h = ramdisk + *off;
        char raw[256];
        char name[101];
        char prefix[156];
        char type;
        uint64_t size;
        size_t rl;

        if (h[0] == 0) {
            return false;
        }

        memcpy(name, h, 100);
        name[100] = 0;
        size = parse_octal((const char *)h + 124, 12);
        type = (char)h[156];

        raw[0] = 0;
        if (memcmp(h + 257, "ustar", 5) == 0 && h[345] != 0) {
            memcpy(prefix, h + 345, 155);
            prefix[155] = 0;
            k_strlcpy(raw, prefix, sizeof(raw));
            k_strlcat(raw, "/", sizeof(raw));
        }
        k_strlcat(raw, name, sizeof(raw));

        *off += 512 + ((size + 511) & ~511ULL);

        if (type == 'x' || type == 'g' || type == 'L' || type == 'K') {
            continue;
        }

        rl = k_strlen(raw);
        e->is_dir = (type == '5') || (rl > 0 && raw[rl - 1] == '/');
        normalise_name(raw, e->path, sizeof(e->path));
        e->size = e->is_dir ? 0 : size;
        e->data = h + 512;

        if (e->path[0] == 0) {
            continue;           /* the root entry */
        }
        return true;
    }
    return false;
}

static bool fs_lookup(const char *path, fsent_t *out)
{
    uint64_t off = 0;
    fsent_t e;

    if (path[0] == 0) {
        out->path[0] = 0;
        out->size = 0;
        out->data = NULL;
        out->is_dir = true;
        return true;
    }
    if (!ramdisk) {
        return false;
    }

    while (tar_next(&off, &e)) {
        if (k_stricmp(e.path, path) == 0) {
            *out = e;
            return true;
        }
    }
    return false;
}

/* True if entry path is a direct child of dir. */
static bool is_child_of(const char *entpath, const char *dir, const char **base)
{
    size_t dl = k_strlen(dir);
    const char *p = entpath;

    if (dl) {
        if (k_strnicmp(p, dir, dl) != 0 || p[dl] != '/') {
            return false;
        }
        p += dl + 1;
    }
    if (*p == 0) {
        return false;
    }
    for (const char *q = p; *q; q++) {
        if (*q == '/') {
            return false;
        }
    }
    *base = p;
    return true;
}

/* Turn user input into a clean path like "docs/sub" (no leading slash). */
static void resolve_path(const char *in, char *out, size_t outsz)
{
    char full[300];
    char *comp[32];
    size_t count = 0;
    char *p;

    full[0] = 0;
    if (in[0] != '\\' && in[0] != '/') {
        k_strlcpy(full, cwd, sizeof(full));
        k_strlcat(full, "/", sizeof(full));
    }
    k_strlcat(full, in, sizeof(full));

    for (p = full; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }

    p = full;
    while (*p) {
        char *start = p;
        while (*p && *p != '/') {
            p++;
        }
        if (*p) {
            *p = 0;
            p++;
        }

        if (start[0] == 0 || (start[0] == '.' && start[1] == 0)) {
            continue;
        }
        if (start[0] == '.' && start[1] == '.' && start[2] == 0) {
            if (count > 0) {
                count--;
            }
            continue;
        }
        if (count < 32) {
            comp[count++] = start;
        }
    }

    out[0] = 0;
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            k_strlcat(out, "/", outsz);
        }
        k_strlcat(out, comp[i], outsz);
    }
}

static void to_dos_path(const char *path, char *out, size_t outsz)
{
    size_t i = 0;
    out[0] = 0;
    k_strlcpy(out, "A:\\", outsz);
    i = k_strlen(out);
    for (const char *p = path; *p && i + 1 < outsz; p++) {
        out[i++] = (*p == '/') ? '\\' : *p;
    }
    out[i] = 0;
}

/* ------------------------------------------------------------------ */
/* Shell commands                                                      */
/* ------------------------------------------------------------------ */

typedef void (*cmd_fn)(char *args);

typedef struct {
    const char *name;
    cmd_fn      fn;
    const char *usage;      /* NULL = hidden alias */
    const char *help;
} command_t;

static char history[HISTORY_MAX][INPUT_SIZE];
static size_t hist_count = 0;

static bool next_token(char **cursor, char *out, size_t max)
{
    char *p = *cursor;
    size_t n = 0;

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == 0) {
        *cursor = p;
        out[0] = 0;
        return false;
    }
    while (*p && *p != ' ' && *p != '\t') {
        if (n + 1 < max) {
            out[n++] = *p;
        }
        p++;
    }
    out[n] = 0;
    *cursor = p;
    return true;
}

static uint32_t hex_digit(char c)
{
    if (c >= '0' && c <= '9') return (uint32_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint32_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint32_t)(c - 'A' + 10);
    return 0xFF;
}

static void cmd_help(char *args);

static void cmd_cls(char *args)
{
    (void)args;
    con_clear();
}

static void cmd_ver(char *args)
{
    (void)args;
    kprintf("AIOS version %s (BIOS kernel)\n", AIOS_VERSION);
}

static void cmd_about(char *args)
{
    (void)args;
    kprintf("\nAIOS - A personal operating system project\n");
    kprintf("Modern, fast, and DOS-friendly.\n");
    kprintf("Bare-metal x86_64 kernel, booted by Limine.\n\n");
}

static void cmd_echo(char *args)
{
    kprintf("%s\n", args);
}

static void cmd_time(char *args)
{
    rtc_time_t t;
    (void)args;
    rtc_get(&t);
    kprintf("%02u:%02u:%02u\n", t.hour, t.minute, t.second);
}

static void cmd_date(char *args)
{
    rtc_time_t t;
    (void)args;
    rtc_get(&t);
    kprintf("%04u-%02u-%02u\n", t.year, t.month, t.day);
}

static void cmd_history(char *args)
{
    (void)args;
    for (size_t i = 0; i < hist_count; i++) {
        kprintf("%3u  %s\n", (unsigned)(i + 1), history[i]);
    }
}

static void cmd_color(char *args)
{
    char arg[16];
    char *c = args;
    uint32_t bg, fg;

    if (!next_token(&c, arg, sizeof(arg))) {
        con_set_color(7, 0);
        con_clear();
        return;
    }

    if (k_strlen(arg) != 2 || hex_digit(arg[0]) > 15 || hex_digit(arg[1]) > 15) {
        kprintf("Usage: COLOR <bg><fg>   e.g. COLOR 0A (green on black)\n");
        kprintf("0 Black  1 Blue   2 Green   3 Cyan   4 Red   5 Magenta  6 Brown  7 Gray\n");
        kprintf("8 DkGray 9 LtBlue A LtGreen B LtCyan C LtRed D LtMagenta E Yellow  F White\n");
        return;
    }

    bg = hex_digit(arg[0]);
    fg = hex_digit(arg[1]);
    if (bg == fg) {
        kprintf("Foreground and background must differ.\n");
        return;
    }

    con_set_color((uint8_t)fg, (uint8_t)bg);
    con_clear();
}

static void cmd_mem(char *args)
{
    uint64_t usable = 0, reclaim = 0, total = 0;
    struct limine_memmap_response *mm = memmap_request.response;
    (void)args;

    if (!mm) {
        kprintf("Memory map not available.\n");
        return;
    }

    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE) {
            usable += e->length;
            total += e->length;
        } else if (e->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
                   e->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE) {
            reclaim += e->length;
            total += e->length;
        } else if (e->type == LIMINE_MEMMAP_KERNEL_AND_MODULES ||
                   e->type == LIMINE_MEMMAP_ACPI_NVS ||
                   e->type == LIMINE_MEMMAP_FRAMEBUFFER) {
            total += e->length;
        }
    }

    kprintf("\nMemory\n------\n");
    kprintf("Usable:      %lu MB\n", usable >> 20);
    kprintf("Reclaimable: %lu MB\n", reclaim >> 20);
    kprintf("Total seen:  %lu MB\n\n", total >> 20);
}

static void cmd_cpu(char *args)
{
    uint32_t a, b, c, d, maxext;
    char vendor[13];
    char brand[49];
    char *bp;
    (void)args;

    cpuid(0, &a, &b, &c, &d);
    memcpy(vendor, &b, 4);
    memcpy(vendor + 4, &d, 4);
    memcpy(vendor + 8, &c, 4);
    vendor[12] = 0;

    kprintf("\nCPU\n---\n");
    kprintf("Vendor: %s\n", vendor);

    cpuid(0x80000000u, &maxext, &b, &c, &d);
    if (maxext >= 0x80000004u) {
        for (uint32_t i = 0; i < 3; i++) {
            cpuid(0x80000002u + i, &a, &b, &c, &d);
            memcpy(brand + i * 16, &a, 4);
            memcpy(brand + i * 16 + 4, &b, 4);
            memcpy(brand + i * 16 + 8, &c, 4);
            memcpy(brand + i * 16 + 12, &d, 4);
        }
        brand[48] = 0;
        bp = brand;
        while (*bp == ' ') {
            bp++;
        }
        kprintf("Name:   %s\n", bp);
    }
    kprintf("\n");
}

static void cmd_screen(char *args)
{
    (void)args;
    kprintf("Framebuffer: %lux%lu, text grid %ux%u\n",
            (unsigned long)fb_w, (unsigned long)fb_h, cols, rows);
}

static void cmd_dir(char *args)
{
    char arg[128], path[128], shown[160];
    char *c = args;
    fsent_t dir, e;
    uint64_t off = 0, bytes = 0;
    unsigned files = 0, dirs = 0;
    const char *base;

    if (!ramdisk) {
        kprintf("No RAM disk loaded.\n");
        return;
    }

    if (!next_token(&c, arg, sizeof(arg))) {
        k_strlcpy(arg, ".", sizeof(arg));
    }

    resolve_path(arg, path, sizeof(path));
    if (!fs_lookup(path, &dir) || !dir.is_dir) {
        kprintf("File not found: %s\n", arg);
        return;
    }

    to_dos_path(path, shown, sizeof(shown));
    kprintf("\n Directory of %s\n\n", shown);

    while (tar_next(&off, &e)) {
        if (!is_child_of(e.path, path, &base)) {
            continue;
        }
        if (e.is_dir) {
            kprintf("  %-24s     <DIR>\n", base);
            dirs++;
        } else {
            kprintf("  %-24s %9lu\n", base, (unsigned long)e.size);
            files++;
            bytes += e.size;
        }
    }

    kprintf("\n  %u file(s)   %lu bytes\n  %u dir(s)\n", files, (unsigned long)bytes, dirs);
}

static void cmd_cd(char *args)
{
    char arg[128], path[128], shown[160];
    char *c = args;
    fsent_t e;

    if (!next_token(&c, arg, sizeof(arg))) {
        to_dos_path(cwd, shown, sizeof(shown));
        kprintf("%s\n", shown);
        return;
    }

    resolve_path(arg, path, sizeof(path));
    if (!fs_lookup(path, &e)) {
        kprintf("The system cannot find the path specified.\n");
        return;
    }
    if (!e.is_dir) {
        kprintf("The directory name is invalid.\n");
        return;
    }
    k_strlcpy(cwd, path, sizeof(cwd));
}

static void cmd_type(char *args)
{
    char arg[128], path[128];
    char *c = args;
    fsent_t e;

    if (!next_token(&c, arg, sizeof(arg))) {
        kprintf("Usage: TYPE <file>\n");
        return;
    }

    resolve_path(arg, path, sizeof(path));
    if (!fs_lookup(path, &e) || path[0] == 0) {
        kprintf("File not found: %s\n", arg);
        return;
    }
    if (e.is_dir) {
        kprintf("Access denied: %s is a directory.\n", arg);
        return;
    }

    cursor_hide();
    for (uint64_t i = 0; i < e.size; i++) {
        char ch = (char)e.data[i];
        if (ch == '\n' || ch == '\t' || (ch >= ' ' && ch <= '~')) {
            putc_raw(ch);
        }
    }
    putc_raw('\n');
    cursor_show();
}

static void cmd_reboot(char *args)
{
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) null_idt = { 0, 0 };
    int guard = 0;
    (void)args;

    kprintf("Rebooting...\n");

    while ((inb(0x64) & 0x02) && guard++ < 100000) {
        /* wait for controller input buffer to empty */
    }
    outb(0x64, 0xFE);

    /* Fallback: triple fault. */
    __asm__ volatile ("lidt %0\n\tint3" : : "m"(null_idt));
    hcf();
}

static void cmd_shutdown(char *args)
{
    (void)args;
    kprintf("Shutting down...\n");

    outw(0x604, 0x2000);        /* QEMU */
    outw(0xB004, 0x2000);       /* Bochs / old QEMU */
    outw(0x4004, 0x3400);       /* VirtualBox */

    con_set_color(14, 0);
    kprintf("\nIt is now safe to turn off your computer.\n");
    hcf();
}

static const command_t commands[] = {
    { "HELP",     cmd_help,     "HELP",              "Show this command list"        },
    { "CLEAR",    cmd_cls,      "CLEAR, CLS, CLR",   "Clear the screen"              },
    { "CLS",      cmd_cls,      NULL,                NULL                            },
    { "CLR",      cmd_cls,      NULL,                NULL                            },
    { "VER",      cmd_ver,      "VER, VERSION",      "Show the AIOS version"         },
    { "VERSION",  cmd_ver,      NULL,                NULL                            },
    { "ABOUT",    cmd_about,    "ABOUT",             "About AIOS"                    },
    { "ECHO",     cmd_echo,     "ECHO <text>",       "Print a message"               },
    { "TIME",     cmd_time,     "TIME",              "Show the system time"          },
    { "DATE",     cmd_date,     "DATE",              "Show the system date"          },
    { "DIR",      cmd_dir,      "DIR, LS [path]",    "List files and folders"        },
    { "LS",       cmd_dir,      NULL,                NULL                            },
    { "CD",       cmd_cd,       "CD [path]",         "Change or show the directory"  },
    { "CHDIR",    cmd_cd,       NULL,                NULL                            },
    { "TYPE",     cmd_type,     "TYPE, CAT <file>",  "Show a text file"              },
    { "CAT",      cmd_type,     NULL,                NULL                            },
    { "COLOR",    cmd_color,    "COLOR [bg fg]",     "Set colors, e.g. COLOR 0A"     },
    { "MEM",      cmd_mem,      "MEM",               "Show memory information"       },
    { "CPU",      cmd_cpu,      "CPU",               "Show processor information"    },
    { "SCREEN",   cmd_screen,   "SCREEN",            "Show screen resolution"        },
    { "HISTORY",  cmd_history,  "HISTORY",           "Show command history"          },
    { "REBOOT",   cmd_reboot,   "REBOOT",            "Restart the computer"          },
    { "SHUTDOWN", cmd_shutdown, "SHUTDOWN",          "Power off the computer"        }
};

#define COMMAND_COUNT  (sizeof(commands) / sizeof(commands[0]))

static void cmd_help(char *args)
{
    (void)args;
    kprintf("\nAvailable AIOS commands:\n");
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        if (commands[i].usage) {
            kprintf("  %-20s %s\n", commands[i].usage, commands[i].help);
        }
    }
    kprintf("\nTips: Up/Down = history, Left/Right/Home/End = edit, Esc = clear line.\n");
}

/* ------------------------------------------------------------------ */
/* Line editor                                                         */
/* ------------------------------------------------------------------ */

static void add_history(const char *line)
{
    if (line[0] == 0) {
        return;
    }
    if (hist_count > 0 &&
        memcmp(history[hist_count - 1], line, k_strlen(line) + 1) == 0) {
        return;
    }
    if (hist_count == HISTORY_MAX) {
        for (size_t i = 1; i < HISTORY_MAX; i++) {
            memcpy(history[i - 1], history[i], INPUT_SIZE);
        }
        hist_count--;
    }
    k_strlcpy(history[hist_count], line, INPUT_SIZE);
    hist_count++;
}

static void redraw(uint32_t sx, uint32_t sy, const char *buf, size_t len,
                   size_t cur, size_t prev)
{
    cursor_hide();
    cx = sx;
    cy = sy;
    for (size_t i = 0; i < len; i++) {
        putc_raw(buf[i]);
    }
    for (size_t i = len; i < prev; i++) {
        putc_raw(' ');
    }
    cx = sx + (uint32_t)cur;
    cy = sy;
    cursor_show();
}

static void read_line(char *buf, size_t size)
{
    size_t len = 0, cur = 0, prev;
    size_t histpos = hist_count;
    size_t maxlen = size - 1;
    uint32_t sx = cx, sy = cy;
    char draft[INPUT_SIZE];

    buf[0] = 0;
    draft[0] = 0;

    /* Keep the edit line on one screen row so cursor math stays simple. */
    if (cols > sx + 1 && (cols - sx - 1) < maxlen) {
        maxlen = cols - sx - 1;
    }

    for (;;) {
        int k = kbd_read();
        prev = len;

        if (k == '\n') {
            con_putc('\n');
            return;
        }

        if (k == '\b') {
            if (cur > 0) {
                memmove(&buf[cur - 1], &buf[cur], len - cur + 1);
                cur--;
                len--;
            }
        } else if (k >= ' ' && k <= '~') {
            if (len < maxlen) {
                memmove(&buf[cur + 1], &buf[cur], len - cur + 1);
                buf[cur] = (char)k;
                cur++;
                len++;
            }
        } else if (k == 27) {
            buf[0] = 0;
            len = cur = 0;
        } else if (k == KEY_LEFT) {
            if (cur > 0) cur--;
        } else if (k == KEY_RIGHT) {
            if (cur < len) cur++;
        } else if (k == KEY_HOME) {
            cur = 0;
        } else if (k == KEY_END) {
            cur = len;
        } else if (k == KEY_DELETE) {
            if (cur < len) {
                memmove(&buf[cur], &buf[cur + 1], len - cur);
                len--;
            }
        } else if (k == KEY_UP) {
            if (histpos > 0) {
                if (histpos == hist_count) {
                    k_strlcpy(draft, buf, sizeof(draft));
                }
                histpos--;
                k_strlcpy(buf, history[histpos], size);
                len = k_strlen(buf);
                if (len > maxlen) {
                    buf[maxlen] = 0;
                    len = maxlen;
                }
                cur = len;
            }
        } else if (k == KEY_DOWN) {
            if (histpos < hist_count) {
                histpos++;
                if (histpos == hist_count) {
                    k_strlcpy(buf, draft, size);
                } else {
                    k_strlcpy(buf, history[histpos], size);
                }
                len = k_strlen(buf);
                if (len > maxlen) {
                    buf[maxlen] = 0;
                    len = maxlen;
                }
                cur = len;
            }
        }

        redraw(sx, sy, buf, len, cur, prev);
    }
}

/* ------------------------------------------------------------------ */
/* Command dispatcher, prompt, banner                                  */
/* ------------------------------------------------------------------ */

static void run_command(char *line)
{
    char command[INPUT_SIZE];
    char dotdot[] = "..";
    char rootdir[] = "\\";
    char *argument;
    size_t length = 0;

    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == 0) {
        return;
    }

    while (line[length] && line[length] != ' ' && line[length] != '\t' &&
           length < INPUT_SIZE - 1) {
        command[length] = k_up(line[length]);
        length++;
    }
    command[length] = 0;

    argument = line + length;
    while (*argument == ' ' || *argument == '\t') {
        argument++;
    }

    /* DOS-style shortcuts: "CD.." and "CD\" */
    if (k_stricmp(command, "CD..") == 0) {
        k_strlcpy(command, "CD", sizeof(command));
        argument = dotdot;
    } else if (k_stricmp(command, "CD\\") == 0) {
        k_strlcpy(command, "CD", sizeof(command));
        argument = rootdir;
    }

    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        if (k_stricmp(command, commands[i].name) == 0) {
            commands[i].fn(argument);
            return;
        }
    }

    kprintf("Unknown command: %s\n", command);
    kprintf("Type HELP to see available commands.\n");
}

static void print_prompt(void)
{
    char shown[160];

    to_dos_path(cwd, shown, sizeof(shown));

    con_putc('\n');
    con_set_color(10, cur_bg);
    con_write("AIOS ");
    con_set_color(14, cur_bg);
    con_write(shown);
    con_set_color(10, cur_bg);
    con_write("> ");
    con_set_color(7, cur_bg);
}

static void print_banner(void)
{
    uint8_t bg = cur_bg;

    con_set_color(11, bg);
    kprintf("====================================\n");
    kprintf("           Welcome to AIOS\n");
    kprintf("====================================\n");
    con_set_color(7, bg);
    kprintf("AIOS shell version %s\n", AIOS_VERSION);
    kprintf("Type HELP to see available commands.\n");

    if (!ramdisk) {
        con_set_color(12, bg);
        kprintf("Warning: no RAM disk was loaded.\n");
        con_set_color(7, bg);
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

void _start(void)
{
    char input[INPUT_SIZE];

    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    if (!con_init(framebuffer_request.response->framebuffers[0])) {
        hcf();
    }

    if (module_request.response != NULL &&
        module_request.response->module_count > 0) {
        struct limine_file *f = module_request.response->modules[0];
        ramdisk = (const uint8_t *)f->address;
        ramdisk_size = f->size;
    }

    kbd_flush();
    print_banner();

    for (;;) {
        print_prompt();
        read_line(input, sizeof(input));
        add_history(input);
        run_command(input);
    }
}
