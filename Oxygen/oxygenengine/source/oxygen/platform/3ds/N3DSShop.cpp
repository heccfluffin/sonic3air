/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: what was bought with Play Coins in the "3DS" menu, and the choices made there.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSShop.h"
#include "oxygen/platform/3ds/N3DSFileWriter.h"
#include "oxygen/platform/3ds/N3DSProfile.h"

#include <stdio.h>
#include <string.h>
#include <initializer_list>


namespace n3ds
{
	namespace shop
	{
		namespace
		{
			const char* FILE_PATH = "sdmc:/3ds/sonic3air/savedata/3ds_shop.txt";
			Data gData;
			Data gSavedData;		// As last loaded / saved, to save only if something changed
			bool gLoaded = false;

			bool operator!=(const Data& a, const Data& b)
			{
				return a.mOwnedThemes != b.mOwnedThemes || a.mSelectedTheme != b.mSelectedTheme || a.mOwnsShield != b.mOwnsShield ||
					   a.mSelectedShield != b.mSelectedShield || a.mShowMenu != b.mShowMenu || a.mCustomTheme != b.mCustomTheme ||
					   a.mNightMode != b.mNightMode || a.mNightStart != b.mNightStart || a.mNightEnd != b.mNightEnd;
			}

			void load()
			{
				gLoaded = true;
				FILE* file = fopen(FILE_PATH, "r");
				if (nullptr == file)
				{
					gSavedData = gData;
					return;
				}

				// Simple "key=value" lines
				char line[384];
				while (nullptr != fgets(line, sizeof(line), file))
				{
					unsigned int value = 0;
					if (strncmp(line, "custom_theme=", 13) == 0)
					{
						gData.mCustomTheme = line + 13;
						while (!gData.mCustomTheme.empty() && (gData.mCustomTheme.back() == '\n' || gData.mCustomTheme.back() == '\r'))
							gData.mCustomTheme.pop_back();
					}
					else if (sscanf(line, "owned_themes=%x", &value) == 1)
						gData.mOwnedThemes = (value | (1 << THEME_BLUE)) & ((1 << _NUM_THEMES) - 1);
					else if (sscanf(line, "theme=%u", &value) == 1)
						gData.mSelectedTheme = (int)value;
					else if (sscanf(line, "owns_shield=%u", &value) == 1)
						gData.mOwnsShield = (value != 0);
					else if (sscanf(line, "shield=%u", &value) == 1)
						gData.mSelectedShield = (int)value;
					else if (sscanf(line, "show_menu=%u", &value) == 1)
						gData.mShowMenu = (value != 0);
					else if (sscanf(line, "night_mode=%u", &value) == 1)
						gData.mNightMode = (value < _NUM_NIGHT_MODES) ? (int)value : NIGHT_MODE_OFF;
					else if (sscanf(line, "night_start=%u", &value) == 1 && value < 24 * 60)
						gData.mNightStart = (int)value;
					else if (sscanf(line, "night_end=%u", &value) == 1 && value < 24 * 60)
						gData.mNightEnd = (int)value;
				}
				fclose(file);

				// Only allow what was bought
				if (gData.mSelectedTheme == THEME_AUTO)
				{
					// Free, always allowed
				}
				else if (gData.mSelectedTheme == THEME_CUSTOM)
				{
					if (gData.mCustomTheme.empty())
						gData.mSelectedTheme = THEME_BLUE;
				}
				else if (gData.mSelectedTheme < 0 || gData.mSelectedTheme >= _NUM_THEMES || (gData.mOwnedThemes & (1 << gData.mSelectedTheme)) == 0 || gData.mSelectedTheme == THEME_AERO)
				{
					gData.mSelectedTheme = THEME_BLUE;
				}
				if (!gData.mOwnsShield || gData.mSelectedShield < 0 || gData.mSelectedShield >= _NUM_SHIELDS)
					gData.mSelectedShield = SHIELD_OFF;
				gSavedData = gData;
			}
		}

		Data& get()
		{
			if (!gLoaded)
				load();
			return gData;
		}

		void save()
		{
			if (!gLoaded || !(gData != gSavedData))
				return;

			// Written in the background: the SD card can take seconds to respond at times, which froze the 3DS menu on selecting something
			char text[256];
			snprintf(text, sizeof(text), "owned_themes=%x\ntheme=%d\nowns_shield=%d\nshield=%d\nshow_menu=%d\n", (unsigned int)gData.mOwnedThemes, gData.mSelectedTheme,
					 gData.mOwnsShield ? 1 : 0, gData.mSelectedShield, gData.mShowMenu ? 1 : 0);
			std::string content = text;
			content += "night_mode=" + std::to_string(gData.mNightMode) + "\n";
			content += "night_start=" + std::to_string(gData.mNightStart) + "\n";
			content += "night_end=" + std::to_string(gData.mNightEnd) + "\n";
			if (!gData.mCustomTheme.empty())
				content += "custom_theme=" + gData.mCustomTheme + "\n";
			n3ds::filewriter::writeFile(FILE_PATH, std::vector<uint8_t>(content.begin(), content.end()), [](bool success, uint32_t)
			{
				if (!success)
					RMX_LOG_INFO("3DS shop: could not save " << FILE_PATH);
			});
			gSavedData = gData;
		}

		int resolveTheme(int theme)
		{
			if (theme != THEME_AUTO)
				return theme;

			const int hour = n3ds::profile::getLocalTime().mHour;

			const uint32_t owned = get().mOwnedThemes;
			const auto pick = [owned](std::initializer_list<int> themes)
			{
				for (int theme : themes)
				{
					if (owned & (1 << theme))
						return theme;
				}
				return (int)THEME_BLUE;
			};
			if (hour >= 6 && hour < 9)
				return pick({ THEME_AURORA, THEME_BLUE });
			if (hour >= 9 && hour < 17)
				return THEME_BLUE;
			if (hour >= 17 && hour < 20)
				return pick({ THEME_SUNSET, THEME_NIGHT, THEME_BLUE });
			return pick({ THEME_NIGHT, THEME_SUNSET, THEME_BLUE });
		}

		bool isNightModeActive()
		{
			switch (get().mNightMode)
			{
				case NIGHT_MODE_ALWAYS:
					return true;
				case NIGHT_MODE_AFTER_DARK:
				{
					const n3ds::profile::LocalTime now = n3ds::profile::getLocalTime();
					const int minute = now.mHour * 60 + now.mMinute;
					const int start = get().mNightStart;
					const int end = get().mNightEnd;
					if (start == end)
						return false;
					if (start < end)
						return (minute >= start && minute < end);		// E.g. 1:00 to 7:00
					return (minute >= start || minute < end);			// Over midnight, e.g. 20:00 to 6:00
				}
				default:
					return false;
			}
		}

		uint8_t getShieldFlags(int shield)
		{
			switch (shield)
			{
				case SHIELD_CLASSIC:	return 0x01;
				case SHIELD_FIRE:		return 0x10;
				case SHIELD_LIGHTNING:	return 0x20;
				case SHIELD_BUBBLE:		return 0x40;
				default:				return 0;
			}
		}
	}
}

#endif
