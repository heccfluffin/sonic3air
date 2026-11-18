/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: loading screen, shown from the moment the game opens until its first frame.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSSplash.h"
#if defined(N3DS_GPU_PRESENT)
	#include "oxygen/platform/3ds/N3DSGpuPresenter.h"
#endif

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "s3air_splash_bin.h"		// The picture: 400 x 240 RGBA, zlib-compressed (see build/_3ds/splash/make_splash.py)


namespace n3ds
{
	namespace splash
	{
		namespace
		{
			const constexpr int WIDTH = 400;
			const constexpr int HEIGHT = 240;

			uint8* gPixels = nullptr;		// RGBA, i.e. the software drawer's ABGR8888 format
			bool gOwnGraphics = false;
			PrintConsole gConsole;

			bool decompress()
			{
				if (nullptr != gPixels)
					return true;
				const uLongf expectedSize = WIDTH * HEIGHT * 4;
				gPixels = (uint8*)malloc(expectedSize);
				if (nullptr == gPixels)
					return false;
				uLongf size = expectedSize;
				if (uncompress(gPixels, &size, s3air_splash_bin, s3air_splash_bin_size) != Z_OK || size != expectedSize)
				{
					free(gPixels);
					gPixels = nullptr;
					return false;
				}
				return true;
			}
		}

		void showEarly()
		{
			if (!decompress())
				return;

			gfxInitDefault();		// Top and bottom framebuffers in BGR8
			gOwnGraphics = true;
			gfxSetDoubleBuffering(GFX_TOP, false);

			// The top screen's framebuffer is rotated: 240 x 400, stored column by column, starting at the bottom left
			u16 width = 0;
			u16 height = 0;
			u8* framebuffer = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &width, &height);
			if (nullptr != framebuffer && width == HEIGHT && height == WIDTH)
			{
				for (int x = 0; x < WIDTH; ++x)
				{
					u8* column = &framebuffer[x * HEIGHT * 3];
					for (int y = 0; y < HEIGHT; ++y)
					{
						const uint8* src = &gPixels[(y * WIDTH + x) * 4];
						u8* dst = &column[(HEIGHT - 1 - y) * 3];
						dst[0] = src[2];	// B
						dst[1] = src[1];	// G
						dst[2] = src[0];	// R
					}
				}
			}

			consoleInit(GFX_BOTTOM, &gConsole);
			consoleSelect(&gConsole);
			gConsole.cursorX = 1;
			gConsole.cursorY = 14;
			printf("               Loading...");

			gfxFlushBuffers();
			gfxScreenSwapBuffers(GFX_TOP, false);
			gfxScreenSwapBuffers(GFX_BOTTOM, false);
			gspWaitForVBlank();
		}

		void endEarly()
		{
			if (gOwnGraphics)
			{
				// Clear the framebuffers before releasing them: SDL's graphics setup reuses this memory without clearing it, and would
				// otherwise show the loading screen's BGR8 pixels interpreted as RGBA8 - a glitched mess - until the first frame
				for (gfxScreen_t screen : { GFX_TOP, GFX_BOTTOM })
				{
					u16 width = 0;
					u16 height = 0;
					u8* framebuffer = gfxGetFramebuffer(screen, GFX_LEFT, &width, &height);
					if (nullptr != framebuffer)
					{
						memset(framebuffer, 0, (size_t)width * height * 3);
						GSPGPU_FlushDataCache(framebuffer, (u32)width * height * 3);
					}
				}
				gfxScreenSwapBuffers(GFX_TOP, false);
				gfxScreenSwapBuffers(GFX_BOTTOM, false);
				gspWaitForVBlank();

				gfxExit();
				gOwnGraphics = false;
			}
		}

		void showAfterVideoInit()
		{
			if (nullptr == gPixels)
				return;
		#if defined(N3DS_GPU_PRESENT)
			// Twice: the top screen alternates between two framebuffers, both should show the loading screen until the first game frame
			for (int k = 0; k < 2; ++k)
			{
				n3ds::gpu::present(gPixels, WIDTH, HEIGHT, WIDTH * 4, 0, 0, WIDTH, HEIGHT);
				gspWaitForVBlank();
			}
		#endif
			free(gPixels);
			gPixels = nullptr;
		}
	}
}

#endif
