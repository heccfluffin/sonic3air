/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "PlatformDefinitions.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSBottomScreen.h"

#include <3ds.h>
#include <string.h>

// Generated at build time from Oxygen/sonic3air/build/_3ds/bottomscreen/controls.bin:
// RGB565 image, already rotated to the bottom screen's framebuffer layout (240 x 320)
#include "controls_bin.h"


namespace n3ds
{
	namespace
	{
		aptHookCookie gAptHookCookie;
		bool gAptHookInstalled = false;

		void onAptEvent(APT_HookType hookType, void* /*param*/)
		{
			// Coming back from the HOME menu or sleep mode: make sure the image is shown again
			if (hookType == APTHOOK_ONRESTORE || hookType == APTHOOK_ONWAKEUP)
				drawControlsScreen();
		}
	}

	void drawControlsScreen()
	{
		// The image is stored as RGB565, and only needs to be drawn once, so use a single buffer
		gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
		gfxSetDoubleBuffering(GFX_BOTTOM, false);

		u16 width = 0;
		u16 height = 0;
		u8* framebuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &width, &height);
		const size_t size = (size_t)width * (size_t)height * 2;
		if (nullptr != framebuffer && size == controls_bin_size)
		{
			memcpy(framebuffer, controls_bin, size);
			GSPGPU_FlushDataCache(framebuffer, size);
			gfxScreenSwapBuffers(GFX_BOTTOM, false);
		}

		if (!gAptHookInstalled)
		{
			aptHook(&gAptHookCookie, &onAptEvent, nullptr);
			gAptHookInstalled = true;
		}
	}
}

#endif
