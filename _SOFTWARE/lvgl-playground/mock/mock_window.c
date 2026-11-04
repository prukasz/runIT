#define SDL_MAIN_HANDLED
#include "mock_window.h"

#include <SDL.h>
#include <stdio.h>

#include "mock_panel.h"
#include "mock_touch.h"

#define BEZEL 28   /* px of "device body" round the glass */

static struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    int w, h, scale;
    bool pressed;
    int shots;
} s;

int mock_window_open(const char *title, int panel_w, int panel_h, int scale)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return -1;
    s.w = panel_w;
    s.h = panel_h;
    s.scale = scale < 1 ? 1 : scale;
    s.win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, panel_w * s.scale + 2 * BEZEL,
                             panel_h * s.scale + 2 * BEZEL, 0);
    if (!s.win) return -1;
    s.ren = SDL_CreateRenderer(s.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s.ren) s.ren = SDL_CreateRenderer(s.win, -1, SDL_RENDERER_SOFTWARE);
    if (!s.ren) return -1;
    s.tex = SDL_CreateTexture(s.ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, panel_w, panel_h);
    return s.tex ? 0 : -1;
}

void mock_window_close(void)
{
    if (s.tex) SDL_DestroyTexture(s.tex);
    if (s.ren) SDL_DestroyRenderer(s.ren);
    if (s.win) SDL_DestroyWindow(s.win);
    SDL_Quit();
    s.tex = NULL;
    s.ren = NULL;
    s.win = NULL;
}

/* Window pixels to panel pixels, clamped to the glass. */
static void to_panel(int mx, int my, int *x, int *y)
{
    *x = (mx - BEZEL) / s.scale;
    *y = (my - BEZEL) / s.scale;
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (*x >= s.w) *x = s.w - 1;
    if (*y >= s.h) *y = s.h - 1;
}

bool mock_window_poll(void)
{
    SDL_Event e;
    int x, y;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            return false;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button != SDL_BUTTON_LEFT) break;
            s.pressed = true;
            to_panel(e.button.x, e.button.y, &x, &y);
            mock_touch_set(x, y, true);
            break;
        case SDL_MOUSEMOTION:
            if (!s.pressed) break;
            to_panel(e.motion.x, e.motion.y, &x, &y);
            mock_touch_set(x, y, true);
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button != SDL_BUTTON_LEFT) break;
            s.pressed = false;
            to_panel(e.button.x, e.button.y, &x, &y);
            mock_touch_set(x, y, false);
            break;
        case SDL_KEYDOWN:
            if (e.key.keysym.sym == SDLK_ESCAPE) return false;
            if (e.key.keysym.sym == SDLK_s) {
                char path[64];
                snprintf(path, sizeof path, "screenshot_%d.bmp", ++s.shots);
                printf("%s: %s\n", path, mock_panel_save_bmp(path) ? "FAILED" : "saved");
            }
            break;
        }
    }
    return true;
}

void mock_window_present(void)
{
    const uint16_t *fb = mock_panel_framebuffer();
    SDL_Rect glass = {BEZEL, BEZEL, s.w * s.scale, s.h * s.scale};
    if (!fb) return;
    SDL_UpdateTexture(s.tex, NULL, fb, s.w * (int)sizeof(uint16_t));
    SDL_SetRenderDrawColor(s.ren, 28, 30, 34, 255);
    SDL_RenderClear(s.ren);
    SDL_RenderCopy(s.ren, s.tex, NULL, &glass);
    SDL_RenderPresent(s.ren);
}

void mock_window_set_title(const char *title)
{
    SDL_SetWindowTitle(s.win, title);
}

uint32_t mock_window_ticks(void)
{
    return SDL_GetTicks();
}

void mock_window_delay(uint32_t ms)
{
    SDL_Delay(ms);
}
