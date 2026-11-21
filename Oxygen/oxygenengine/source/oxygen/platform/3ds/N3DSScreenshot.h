/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: screenshots of the top screen (L + R).
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include <stdint.h>

namespace n3ds
{
	namespace screenshot
	{
		static const char* const FOLDER = "sdmc:/3ds/sonic3air/screenshots/";

		// Main thread, every frame: takes a screenshot when L and R are pressed together
		void update();

		// Called by the GPU presenter for every frame it shows: if a screenshot was asked for, this frame gets saved.
		// "pixels" in ABGR8888 (bytes R, G, B, A), as the game drew it - before scaling, and without the night mode colors.
		void onPresent(const void* pixels, int width, int height, int pitchBytes);
	}
}

#endif
