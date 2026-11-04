#include "mock_panel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    int width, height;
    uint16_t *fb;
    int x1, x2, y2;     /* the window */
    int cx, cy;         /* the write cursor inside it */
    mock_panel_stats_t stats;
} s;

int mock_panel_init(int width, int height)
{
    mock_panel_deinit();
    s.fb = calloc((size_t)width * height, sizeof(uint16_t));
    if (!s.fb) return -1;
    s.width = width;
    s.height = height;
    return 0;
}

void mock_panel_deinit(void)
{
    free(s.fb);
    memset(&s, 0, sizeof s);
}

void mock_panel_set_window(int x1, int y1, int x2, int y2)
{
    s.x1 = x1;
    s.x2 = x2;
    s.y2 = y2;
    s.cx = x1;
    s.cy = y1;
    s.stats.windows++;
}

void mock_panel_write(const void *pixels, size_t len)
{
    const uint16_t *p = pixels;
    size_t n = len / sizeof(uint16_t);
    s.stats.pixels += (uint32_t)n;
    s.stats.bus_bytes += (uint32_t)len;
    while (n--) {
        if (s.cy <= s.y2 && s.cx < s.width && s.cy < s.height) s.fb[(size_t)s.cy * s.width + s.cx] = *p;
        p++;
        if (++s.cx > s.x2) {
            s.cx = s.x1;
            s.cy++;
        }
    }
}

const mock_panel_stats_t *mock_panel_stats(void)
{
    return &s.stats;
}

const uint16_t *mock_panel_framebuffer(void)
{
    return s.fb;
}

static void put32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

int mock_panel_save_bmp(const char *path)
{
    if (!s.fb) return -1;
    uint32_t row = ((uint32_t)s.width * 3 + 3) & ~3u;
    uint32_t size = 54 + row * s.height;
    uint8_t hdr[54] = {'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 24, 0};
    put32(hdr + 2, size);
    put32(hdr + 18, (uint32_t)s.width);
    put32(hdr + 22, (uint32_t)s.height);
    put32(hdr + 34, row * s.height);

    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(hdr, 1, sizeof hdr, f);
    uint8_t *line = calloc(1, row);
    for (int y = s.height - 1; y >= 0; y--) {   /* BMP is stored bottom-up */
        for (int x = 0; x < s.width; x++) {
            uint16_t p = s.fb[(size_t)y * s.width + x];
            uint8_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
            line[x * 3 + 0] = (uint8_t)((b << 3) | (b >> 2));
            line[x * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
            line[x * 3 + 2] = (uint8_t)((r << 3) | (r >> 2));
        }
        fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
    return 0;
}
