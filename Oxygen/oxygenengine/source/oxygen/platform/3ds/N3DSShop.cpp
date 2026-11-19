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

#include <stdio.h>
#include <string.h>


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
					   a.mSelectedShield != b.mSelectedShield || a.mShowMenu != b.mShowMenu || a.mCustomTheme != b.mCustomTheme;
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
				}
				fclose(file);

				// Only allow what was bought
				if (gData.mSelectedTheme == THEME_CUSTOM)
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
			FILE* file = fopen(FILE_PATH, "w");
			if (nullptr == file)
			{
				RMX_LOG_INFO("3DS shop: could not save " << FILE_PATH);
				return;
			}
			fprintf(file, "owned_themes=%x\ntheme=%d\nowns_shield=%d\nshield=%d\nshow_menu=%d\n", (unsigned int)gData.mOwnedThemes, gData.mSelectedTheme,
					gData.mOwnsShield ? 1 : 0, gData.mSelectedShield, gData.mShowMenu ? 1 : 0);
			if (!gData.mCustomTheme.empty())
				fprintf(file, "custom_theme=%s\n", gData.mCustomTheme.c_str());
			fclose(file);
			gSavedData = gData;
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
