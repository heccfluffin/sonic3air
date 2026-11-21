/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "PlatformDefinitions.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSDialog.h"

#include "oxygen/platform/3ds/N3DSSplash.h"
#include <3ds.h>
#include <SDL.h>
#include <stdio.h>
#include <string>


namespace n3ds
{
	namespace
	{
		// The console breaks lines wherever they reach the screen edge, even in the middle of a word; break them at spaces instead
		std::string wordWrap(const std::string& text, int width)
		{
			if (width <= 1)
				return text;
			std::string result;
			size_t lineStart = 0;		// In "result"
			size_t lastSpace = std::string::npos;
			for (char ch : text)
			{
				if (ch == '\n')
				{
					result += ch;
					lineStart = result.length();
					lastSpace = std::string::npos;
					continue;
				}
				if (ch == ' ')
					lastSpace = result.length();
				result += ch;
				if ((int)(result.length() - lineStart) > width - 1 && lastSpace != std::string::npos && lastSpace > lineStart)
				{
					// Leave one column free: a line that fills the whole width makes the console add an empty line
					result[lastSpace] = '\n';
					lineStart = lastSpace + 1;
					lastSpace = std::string::npos;
				}
			}
			return result;
		}
	}

	int showDialog(const char* caption, const char* text, int numButtons)
	{
		// SDL's 3DS backend can't show message boxes, and before the game window exists nothing would be visible at all.
		// So print the message with libctru's text console instead:
		//  - If SDL video is not running yet, set up the graphics ourselves and use the top screen
		//  - Otherwise use the bottom screen, which the game doesn't use, so the game's top screen stays intact
		const bool ownGraphics = (SDL_WasInit(SDL_INIT_VIDEO) == 0);
		if (ownGraphics)
		{
			n3ds::splash::endEarly();		// In case the loading screen still owns the graphics
			gfxInitDefault();
		}

		PrintConsole console;
		consoleInit(ownGraphics ? GFX_TOP : GFX_BOTTOM, &console);
		consoleSelect(&console);
		consoleClear();

		printf("\x1b[1;33m%s\x1b[0m\n\n", wordWrap((nullptr != caption) ? caption : "Sonic 3 A.I.R.", console.consoleWidth).c_str());
		printf("%s\n\n", wordWrap((nullptr != text) ? text : "", console.consoleWidth).c_str());
		switch (numButtons)
		{
			case 1:  printf("\x1b[1;32mPress A to continue\x1b[0m\n");  break;
			case 2:  printf("\x1b[1;32m[A] OK    [B] Cancel\x1b[0m\n");  break;
			default: printf("\x1b[1;32m[A] Yes   [X] No   [B] Cancel\x1b[0m\n");  break;
		}

		int result = 2;
		while (aptMainLoop())
		{
			hidScanInput();
			const u32 keys = hidKeysDown();
			if (keys & (KEY_A | KEY_START))
			{
				result = 0;
				break;
			}
			if (numButtons >= 3 && (keys & KEY_X))
			{
				result = 1;
				break;
			}
			if (keys & KEY_B)
			{
				result = (numButtons == 1) ? 0 : 2;
				break;
			}
			gfxFlushBuffers();
			gfxSwapBuffers();
			gspWaitForVBlank();
		}

		consoleClear();
		if (ownGraphics)
			gfxExit();
		return result;
	}
}

#endif
