/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_N3DS

#include "../SDL_sysvideo.h"
#include "SDL_n3dsframebuffer_c.h"
#include "SDL_n3dsvideo.h"

#define N3DS_SURFACE "_SDL_N3DSSurface"

typedef struct
{
    int width, height;
} Dimensions;

SDL_FORCE_INLINE void CopyFramebuffertoN3DS_16(u16 *dest, const Dimensions dest_dim, const u16 *source, const Dimensions source_dim);
SDL_FORCE_INLINE void CopyFramebuffertoN3DS_24(u8  *dest, const Dimensions dest_dim, const u8  *source, const Dimensions source_dim);
SDL_FORCE_INLINE void CopyFramebuffertoN3DS_32(u32 *dest, const Dimensions dest_dim, const u32 *source, const Dimensions source_dim);
SDL_FORCE_INLINE void CopyFramebuffertoN3DS_32_ByteReversed(u32 *dest, const Dimensions dest_dim, const u32 *source, const Dimensions source_dim);
SDL_FORCE_INLINE int GetDestOffset(int x, int y, int dest_width);
SDL_FORCE_INLINE int GetSourceOffset(int x, int y, int source_width);
SDL_FORCE_INLINE void FlushN3DSBuffer(const void *buffer, u32 bufsize, gfxScreen_t screen);

int SDL_N3DS_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch)
{
    SDL_Surface *framebuffer;
    SDL_DisplayMode mode;
    int w, h;

    SDL_N3DS_DestroyWindowFramebuffer(_this, window);

    Uint32 surface_format;

    SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window), &mode);
    SDL_GetWindowSizeInPixels(window, &w, &h);

    /* Sonic 3 A.I.R. 3DS port: offer the 32-bit window surface as ABGR8888, which is RGBA8888 with the byte order of
       each pixel reversed. The copy into the framebuffer reverses it back, in the same pass that rotates the image anyway.
       This way, software renderers working in ABGR8888 can draw directly, without a separate full-screen conversion. */
    surface_format = (mode.format == SDL_PIXELFORMAT_RGBA8888) ? SDL_PIXELFORMAT_ABGR8888 : mode.format;
    framebuffer = SDL_CreateRGBSurfaceWithFormat(0, w, h, SDL_BYTESPERPIXEL(surface_format), surface_format);

    if (!framebuffer) {
        return SDL_OutOfMemory();
    }

    SDL_SetWindowData(window, N3DS_SURFACE, framebuffer);
    *format = surface_format;
    *pixels = framebuffer->pixels;
    *pitch = framebuffer->pitch;
    return 0;
}

int SDL_N3DS_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    SDL_WindowData *drv_data = (SDL_WindowData *)window->driverdata;
    SDL_Surface *surface;
    u16 width, height;
    void *framebuffer;
    u32 bufsize;

    surface = (SDL_Surface *)SDL_GetWindowData(window, N3DS_SURFACE);
    if (!surface) {
        return SDL_SetError("%s: Unable to get the window surface.", __func__);
    }

    /* Get the N3DS internal framebuffer and its size */
    framebuffer = gfxGetFramebuffer(drv_data->screen, GFX_LEFT, &width, &height);
    bufsize = width * height * 4;

    if (surface->format->BytesPerPixel == 2)
        CopyFramebuffertoN3DS_16(framebuffer, (Dimensions){ width, height },
                                 surface->pixels, (Dimensions){ surface->w, surface->h });
    else if (surface->format->BytesPerPixel == 3)
        CopyFramebuffertoN3DS_24(framebuffer, (Dimensions){ width, height },
                                 surface->pixels, (Dimensions){ surface->w, surface->h });
    else if (surface->format->format == SDL_PIXELFORMAT_ABGR8888)
        CopyFramebuffertoN3DS_32_ByteReversed(framebuffer, (Dimensions){ width, height },
                                              surface->pixels, (Dimensions){ surface->w, surface->h });
    else
        CopyFramebuffertoN3DS_32(framebuffer, (Dimensions){ width, height },
                                 surface->pixels, (Dimensions){ surface->w, surface->h });
    FlushN3DSBuffer(framebuffer, bufsize, drv_data->screen);

    return 0;
}

SDL_FORCE_INLINE void
CopyFramebuffertoN3DS_16(u16 *dest, const Dimensions dest_dim, const u16 *source, const Dimensions source_dim)
{
    int rows = SDL_min(dest_dim.width, source_dim.height);
    int cols = SDL_min(dest_dim.height, source_dim.width);
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            const u16 *s = source + GetSourceOffset(x, y, source_dim.width);
            u16 *d = dest + GetDestOffset(x, y, dest_dim.width);
            *d = *s;
        }
    }
}

SDL_FORCE_INLINE void
CopyFramebuffertoN3DS_24(u8 *dest, const Dimensions dest_dim, const u8 *source, const Dimensions source_dim)
{
    int rows = SDL_min(dest_dim.width, source_dim.height);
    int cols = SDL_min(dest_dim.height, source_dim.width);
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            const u8 *s = source + GetSourceOffset(x, y, source_dim.width) * 3;
            u8 *d = dest + GetDestOffset(x, y, dest_dim.width) * 3;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
    }
}

SDL_FORCE_INLINE void
CopyFramebuffertoN3DS_32(u32 *dest, const Dimensions dest_dim, const u32 *source, const Dimensions source_dim)
{
    int rows = SDL_min(dest_dim.width, source_dim.height);
    int cols = SDL_min(dest_dim.height, source_dim.width);
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            const u32 *s = source + GetSourceOffset(x, y, source_dim.width);
            u32 *d = dest + GetDestOffset(x, y, dest_dim.width);
            *d = *s;
        }
    }
}

/* Same result as CopyFramebuffertoN3DS_32, plus reversing the byte order of each pixel (ABGR8888 -> RGBA8888).
   The rotation reads source rows but writes destination columns; working in 8x8 blocks keeps both within the
   CPU cache, instead of touching a different cache line for almost every written pixel. */
SDL_FORCE_INLINE void
CopyFramebuffertoN3DS_32_ByteReversed(u32 *dest, const Dimensions dest_dim, const u32 *source, const Dimensions source_dim)
{
    const int rows = SDL_min(dest_dim.width, source_dim.height);
    const int cols = SDL_min(dest_dim.height, source_dim.width);
    const int dest_width = dest_dim.width;
    const int source_width = source_dim.width;
    for (int by = 0; by < rows; by += 8) {
        const int ey = SDL_min(by + 8, rows);
        for (int bx = 0; bx < cols; bx += 8) {
            const int ex = SDL_min(bx + 8, cols);
            for (int x = bx; x < ex; ++x) {
                const u32 *s = source + GetSourceOffset(x, by, source_width);
                u32 *d = dest + GetDestOffset(x, by, dest_width);    /* Moves backwards by one per source line */
                for (int y = by; y < ey; ++y) {
                    *d-- = __builtin_bswap32(*s);
                    s += source_width;
                }
            }
        }
    }
}

SDL_FORCE_INLINE int
GetDestOffset(int x, int y, int dest_width)
{
    return dest_width - y - 1 + dest_width * x;
}

SDL_FORCE_INLINE int
GetSourceOffset(int x, int y, int source_width)
{
    return x + y * source_width;
}

SDL_FORCE_INLINE void
FlushN3DSBuffer(const void *buffer, u32 bufsize, gfxScreen_t screen)
{
    GSPGPU_FlushDataCache(buffer, bufsize);
    gfxScreenSwapBuffers(screen, false);
}

void SDL_N3DS_DestroyWindowFramebuffer(_THIS, SDL_Window *window)
{
    SDL_Surface *surface;
    surface = (SDL_Surface *)SDL_SetWindowData(window, N3DS_SURFACE, NULL);
    SDL_FreeSurface(surface);
}

#endif /* SDL_VIDEO_DRIVER_N3DS */

/* vi: set sts=4 ts=4 sw=4 expandtab: */
