/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the bottom screen - a controls overview with touch buttons for a quick save state,
*	or the profiler's statistics.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSBottomScreen.h"
#include "oxygen/platform/3ds/N3DSProfiler.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "oxygen/platform/3ds/N3DSShop.h"

// The artwork, one per theme, see build/_3ds/bottomscreen/make_bottom_screen.py
#include "s3air_bottom_screen_blue_bin.h"
#include "s3air_bottom_screen_crimson_bin.h"
#include "s3air_bottom_screen_gold_bin.h"
#include "s3air_bottom_screen_emerald_bin.h"
#include "s3air_bottom_screen_night_bin.h"
#include "s3air_bottom_screen_marble_bin.h"
#include "s3air_bottom_screen_sea_bin.h"
#include "s3air_bottom_screen_blocks_bin.h"


namespace n3ds
{
	namespace bottomscreen
	{
		namespace
		{
			const constexpr int SCREEN_WIDTH = 320;
			const constexpr int SCREEN_HEIGHT = 240;

			// Parts of the artwork, in the order make_bottom_screen.py writes them
			enum Part
			{
				PART_FULL_SCREEN,
				PART_SAVE_NORMAL,
				PART_SAVE_PRESSED,
				PART_SAVE_DISABLED,
				PART_LOAD_NORMAL,
				PART_LOAD_PRESSED,
				PART_LOAD_DISABLED,
				PART_STATS_NORMAL,
				PART_STATS_PRESSED,
				PART_STATUS_HINT,			// "Save states work in levels"
				PART_STATUS_SAVING,
				PART_STATUS_SAVED,
				PART_STATUS_LOADING,
				PART_STATUS_LOADED,
				PART_STATUS_NO_STATE,
				PART_STATUS_FAILED,
				PART_STATUS_ONLY_INGAME,
				PART_STATUS_SLOTS,			// Which save file the state belongs to: 9 save files ("No Save", 1-8) x ("has a state", "no state yet")
				_PART_STATUS_SLOTS_END = PART_STATUS_SLOTS + 18 - 1,
				_NUM_PARTS
			};

			struct PartInfo
			{
				int mX = 0;
				int mY = 0;
				int mWidth = 0;
				int mHeight = 0;
				const uint8* mPixels = nullptr;		// RGB565, rows top to bottom (unaligned, read byte-wise)
			};

			// Touch areas, in screen pixels (same as in make_bottom_screen.py)
			struct Rect
			{
				int mX0, mY0, mX1, mY1;
				bool contains(int x, int y) const	{ return x >= mX0 && x < mX1 && y >= mY0 && y < mY1; }
			};
			const Rect SAVE_RECT  = { 206, 40, 314, 108 };
			const Rect LOAD_RECT  = { 206, 116, 314, 184 };
			const Rect STATS_RECT = { 252, 2, 318, 32 };		// A bit larger than the button itself, it's small

			enum Button
			{
				BUTTON_NONE = -1,
				BUTTON_SAVE,
				BUTTON_LOAD,
				BUTTON_STATS
			};

			const char* STATS_SETTING_FILE = "sdmc:/3ds/sonic3air/savedata/bottom_screen_stats";	// Exists if the statistics are shown
			const constexpr double STATUS_SECONDS = 2.5;	// How long a result message stays

			uint8* gData = nullptr;
			PartInfo gParts[_NUM_PARTS];
			bool gInitialized = false;
			bool gDataValid = false;

			bool gShowStats = false;
			volatile bool gNeedsFullRedraw = true;		// Set from the APT hook, i.e. possibly from another thread
			aptHookCookie gAptHookCookie;

			Button gPressedButton = BUTTON_NONE;	// Button the current touch started on
			bool gTouchOnPressedButton = false;		// Whether the touch is still on that button

			// What's currently shown
			Part gShownSave = _NUM_PARTS;
			Part gShownLoad = _NUM_PARTS;
			Part gShownStats = _NUM_PARTS;
			Part gShownStatus = _NUM_PARTS;

			// Status message
			Part gStatusMessage = _NUM_PARTS;		// A temporary message, or _NUM_PARTS for the default
			uint64_t gStatusMessageEnd = 0;			// System tick when the temporary message ends, 0 = stays until replaced

			Request gPendingRequest = Request::NONE;	// Gets returned from the next "update", after "Saving..." / "Loading..." is visible

			uint16 readU16(const uint8* ptr)	{ return (uint16)(ptr[0] | (ptr[1] << 8)); }
			uint32 readU32(const uint8* ptr)	{ return (uint32)(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | ((uint32)ptr[3] << 24)); }

			struct ThemeData
			{
				const uint8* mCompressed;
				const uint32 mCompressedSize;
			};
			const ThemeData THEMES[n3ds::shop::_NUM_THEMES] =
			{
				{ s3air_bottom_screen_blue_bin,    s3air_bottom_screen_blue_bin_size },
				{ nullptr, 0 },		// "Sky Aero", removed
				{ s3air_bottom_screen_crimson_bin, s3air_bottom_screen_crimson_bin_size },
				{ s3air_bottom_screen_gold_bin,    s3air_bottom_screen_gold_bin_size },
				{ s3air_bottom_screen_emerald_bin, s3air_bottom_screen_emerald_bin_size },
				{ s3air_bottom_screen_night_bin,   s3air_bottom_screen_night_bin_size },
				{ s3air_bottom_screen_marble_bin,  s3air_bottom_screen_marble_bin_size },
				{ s3air_bottom_screen_sea_bin,     s3air_bottom_screen_sea_bin_size },
				{ s3air_bottom_screen_blocks_bin,  s3air_bottom_screen_blocks_bin_size },
			};
			int gTheme = -1;		// Theme currently unpacked into "gData"

			bool loadArtworkInternal(int theme)
			{
				// The uncompressed size isn't stored, but it's known: the full screen plus all parts, see make_bottom_screen.py
				const uLongf maxSize = 1024 * 1024;		// Gets shrunk to the actual size afterwards
				gData = (uint8*)malloc(maxSize);
				if (nullptr == gData)
					return false;
				uLongf size = maxSize;
				if (uncompress(gData, &size, THEMES[theme].mCompressed, THEMES[theme].mCompressedSize) != Z_OK || size < 8 || memcmp(gData, "S3BS", 4) != 0)
					return false;
				if (readU16(&gData[4]) != 1 || readU16(&gData[6]) != _NUM_PARTS || size < 8 + 12 * _NUM_PARTS)
					return false;
				uint8* shrunk = (uint8*)realloc(gData, size);
				if (nullptr != shrunk)
					gData = shrunk;

				for (int i = 0; i < _NUM_PARTS; ++i)
				{
					const uint8* entry = &gData[8 + i * 12];
					PartInfo& part = gParts[i];
					part.mX = readU16(&entry[0]);
					part.mY = readU16(&entry[2]);
					part.mWidth = readU16(&entry[4]);
					part.mHeight = readU16(&entry[6]);
					const uint32 offset = readU32(&entry[8]);
					if (part.mX + part.mWidth > SCREEN_WIDTH || part.mY + part.mHeight > SCREEN_HEIGHT || offset + (uLongf)part.mWidth * part.mHeight * 2 > size)
						return false;
					part.mPixels = &gData[offset];
				}
				return true;
			}

			// Unpacks the artwork of a theme (only one is kept in memory). Falls back to the default theme if that fails.
			bool loadArtwork(int theme)
			{
				if (theme < 0 || theme >= n3ds::shop::_NUM_THEMES || nullptr == THEMES[theme].mCompressed)
					theme = n3ds::shop::THEME_BLUE;
				if (theme == gTheme && nullptr != gData)
					return true;

				free(gData);
				gData = nullptr;
				gTheme = -1;
				if (!loadArtworkInternal(theme))
				{
					RMX_LOG_INFO("Bottom screen: could not unpack the artwork of theme " << theme);
					free(gData);
					gData = nullptr;
					if (theme == n3ds::shop::THEME_BLUE || !loadArtworkInternal(n3ds::shop::THEME_BLUE))
					{
						free(gData);
						gData = nullptr;
						return false;
					}
					theme = n3ds::shop::THEME_BLUE;
				}
				gTheme = theme;
				gNeedsFullRedraw = true;
				return true;
			}

			void onAptEvent(APT_HookType hookType, void*)
			{
				// Back from the HOME Menu or sleep mode: draw everything again, the system may have used the screen meanwhile
				if (hookType == APTHOOK_ONRESTORE || hookType == APTHOOK_ONWAKEUP)
					gNeedsFullRedraw = true;
			}

			void setupScreen()
			{
				// RGB565 with a single buffer, like libctru's console (which the profiler and the exit message use) - then they
				// can share the screen without reallocating anything
				if (gfxGetScreenFormat(GFX_BOTTOM) != GSP_RGB565_OES)
					gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
				gfxSetDoubleBuffering(GFX_BOTTOM, false);
			}

			// Draws a part into the framebuffer, which is rotated: 240 x 320, screen pixel (x, y) is at index x * 240 + (239 - y)
			void drawPart(Part partIndex, u16* framebuffer)
			{
				const PartInfo& part = gParts[partIndex];
				for (int x = 0; x < part.mWidth; ++x)
				{
					u16* column = &framebuffer[(part.mX + x) * SCREEN_HEIGHT + (SCREEN_HEIGHT - 1 - part.mY)];
					const uint8* src = &part.mPixels[x * 2];
					for (int y = 0; y < part.mHeight; ++y)
					{
						*column = (u16)(src[0] | (src[1] << 8));
						--column;
						src += part.mWidth * 2;
					}
				}
			}

			int gSlot = -1;					// Save file of the running game: 0 = "No Save", 1-8, or -1 if unknown
			bool gSlotHasState = false;

			Part getStatusPart(bool saveStatesAllowed)
			{
				if (gStatusMessage != _NUM_PARTS)
				{
					if (gStatusMessageEnd == 0 || svcGetSystemTick() < gStatusMessageEnd)
						return gStatusMessage;
					gStatusMessage = _NUM_PARTS;
				}
				if (!saveStatesAllowed || gSlot < 0 || gSlot > 8)
					return PART_STATUS_HINT;
				return (Part)(PART_STATUS_SLOTS + gSlot * 2 + (gSlotHasState ? 0 : 1));
			}

			void setStatusMessage(Part part, bool temporary)
			{
				gStatusMessage = part;
				gStatusMessageEnd = temporary ? (svcGetSystemTick() + (uint64_t)(STATUS_SECONDS * SYSCLOCK_ARM11)) : 0;
			}

			void drawControlsScreen(bool saveStatesAllowed)
			{
				const Part save = !saveStatesAllowed ? PART_SAVE_DISABLED : (gPressedButton == BUTTON_SAVE && gTouchOnPressedButton) ? PART_SAVE_PRESSED : PART_SAVE_NORMAL;
				const Part load = !saveStatesAllowed ? PART_LOAD_DISABLED : (gPressedButton == BUTTON_LOAD && gTouchOnPressedButton) ? PART_LOAD_PRESSED : PART_LOAD_NORMAL;
				const Part stats = (gPressedButton == BUTTON_STATS && gTouchOnPressedButton) ? PART_STATS_PRESSED : PART_STATS_NORMAL;
				const Part status = getStatusPart(saveStatesAllowed);

				const bool fullRedraw = gNeedsFullRedraw;
				if (!fullRedraw && save == gShownSave && load == gShownLoad && stats == gShownStats && status == gShownStatus)
					return;
				if (fullRedraw)
					setupScreen();

				u16 width = 0;
				u16 height = 0;
				u16* framebuffer = (u16*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &width, &height);
				if (nullptr == framebuffer || width != SCREEN_HEIGHT || height != SCREEN_WIDTH)
					return;

				if (fullRedraw)
				{
					gNeedsFullRedraw = false;
					drawPart(PART_FULL_SCREEN, framebuffer);
					gShownSave = gShownLoad = gShownStats = gShownStatus = _NUM_PARTS;
				}
				// The parts overlap a little at their margins (for shadows), so draw changed ones in a fixed order
				if (save != gShownSave)		drawPart(save, framebuffer);
				if (load != gShownLoad)		drawPart(load, framebuffer);
				if (stats != gShownStats)	drawPart(stats, framebuffer);
				if (status != gShownStatus)	drawPart(status, framebuffer);
				gShownSave = save;
				gShownLoad = load;
				gShownStats = stats;
				gShownStatus = status;

				GSPGPU_FlushDataCache(framebuffer, (u32)width * (u32)height * 2);
				gfxScreenSwapBuffers(GFX_BOTTOM, false);
			}

			void setShowStats(bool show)
			{
				gShowStats = show;
				gNeedsFullRedraw = true;
			#if defined(N3DS_PROFILER)
				if (show)
					n3ds::profiler::showOnScreen();
			#endif

				// Remember the choice (just an empty file)
				if (show)
				{
					FILE* file = fopen(STATS_SETTING_FILE, "w");
					if (nullptr != file)
						fclose(file);
				}
				else
				{
					remove(STATS_SETTING_FILE);
				}
			}

			void initialize()
			{
				gInitialized = true;
				gDataValid = loadArtwork(n3ds::shop::get().mSelectedTheme);

			#if defined(N3DS_PROFILER)
				FILE* file = fopen(STATS_SETTING_FILE, "r");
				if (nullptr != file)
				{
					fclose(file);
					gShowStats = true;
					n3ds::profiler::showOnScreen();
				}
			#endif
				aptHook(&gAptHookCookie, &onAptEvent, nullptr);
			}

			Button getButtonAt(int x, int y)
			{
				if (SAVE_RECT.contains(x, y))
					return BUTTON_SAVE;
				if (LOAD_RECT.contains(x, y))
					return BUTTON_LOAD;
				if (STATS_RECT.contains(x, y))
					return BUTTON_STATS;
				return BUTTON_NONE;
			}
		}

		Request update(bool saveStatesAllowed, int slot, bool slotHasState)
		{
			gSlot = slot;
			gSlotHasState = slotHasState;
			if (!gInitialized)
				initialize();
			if (!gDataValid)
				return Request::NONE;
			if (!aptIsActive() || aptShouldClose())		// The screens belong to the system right now
				return Request::NONE;

			// A request from the previous frame: "Saving..." / "Loading..." is visible by now
			if (gPendingRequest != Request::NONE)
			{
				const Request request = gPendingRequest;
				gPendingRequest = Request::NONE;
				if (saveStatesAllowed)
					return request;
				setStatusMessage(PART_STATUS_ONLY_INGAME, true);
			}

			// Touch input (SDL already called "hidScanInput" for this frame)
			const u32 touchDown = hidKeysDown();
			const u32 touchHeld = hidKeysHeld();
			const u32 touchUp = hidKeysUp();
			touchPosition touch;
			hidTouchRead(&touch);

			if (gShowStats)
			{
				// Showing statistics: tapping anywhere goes back to the controls
				if (touchDown & KEY_TOUCH)
				{
					setShowStats(false);
					gPressedButton = BUTTON_NONE;
				}
				else
				{
					return Request::NONE;
				}
			}

			if (touchDown & KEY_TOUCH)
			{
				gPressedButton = getButtonAt(touch.px, touch.py);
				gTouchOnPressedButton = (gPressedButton != BUTTON_NONE);
				if (!saveStatesAllowed && (gPressedButton == BUTTON_SAVE || gPressedButton == BUTTON_LOAD))
				{
					setStatusMessage(PART_STATUS_ONLY_INGAME, true);
					gPressedButton = BUTTON_NONE;
				}
			}
			else if ((touchHeld & KEY_TOUCH) && gPressedButton != BUTTON_NONE)
			{
				// Sliding off the button cancels, like with a mouse
				gTouchOnPressedButton = (getButtonAt(touch.px, touch.py) == gPressedButton);
			}
			else if (gPressedButton != BUTTON_NONE && ((touchUp & KEY_TOUCH) || !(touchHeld & KEY_TOUCH)))
			{
				// Released (the touch position isn't available any more, it's 0 / 0 now)
				const Button released = gTouchOnPressedButton ? gPressedButton : BUTTON_NONE;
				gPressedButton = BUTTON_NONE;
				gTouchOnPressedButton = false;
				switch (released)
				{
					case BUTTON_SAVE:
						if (saveStatesAllowed)
						{
							setStatusMessage(PART_STATUS_SAVING, false);
							gPendingRequest = Request::SAVE_STATE;
						}
						break;

					case BUTTON_LOAD:
						if (saveStatesAllowed)
						{
							setStatusMessage(PART_STATUS_LOADING, false);
							gPendingRequest = Request::LOAD_STATE;
						}
						break;

					case BUTTON_STATS:
					#if defined(N3DS_PROFILER)
						setShowStats(true);
						return Request::NONE;
					#else
						break;
					#endif

					default:
						break;
				}
			}

			drawControlsScreen(saveStatesAllowed);
			return Request::NONE;
		}

		void showResult(Result result)
		{
			switch (result)
			{
				case Result::SAVED:		setStatusMessage(PART_STATUS_SAVED, true);		break;
				case Result::LOADED:	setStatusMessage(PART_STATUS_LOADED, true);		break;
				case Result::NO_STATE:	setStatusMessage(PART_STATUS_NO_STATE, true);	break;
				case Result::FAILED:	setStatusMessage(PART_STATUS_FAILED, true);		break;
			}
		}

		void setTheme(int theme)
		{
			if (!gInitialized)
				return;		// Gets loaded on initialization anyway
			gDataValid = loadArtwork(theme);
		}

		bool isShowingStats()
		{
			return gShowStats || !gDataValid;
		}
	}
}

#endif
