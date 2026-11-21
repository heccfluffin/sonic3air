/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: what was bought with Play Coins in the "3DS" menu, and the choices made there.
*	Stored in "savedata/3ds_shop.txt".
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include <stdint.h>
#include <string>


namespace n3ds
{
	namespace shop
	{
		// Bottom screen themes, in the order of THEMES in build/_3ds/bottomscreen/make_bottom_screen.py
		enum Theme
		{
			THEME_BLUE = 0,		// Default, free
			THEME_AERO,			// Removed ("Sky Aero"), the number stays reserved so saved choices keep pointing at the right themes
			THEME_CRIMSON,
			THEME_GOLD,
			THEME_EMERALD,
			THEME_NIGHT,
			THEME_MARBLE,
			THEME_SEA,
			THEME_BLOCKS,
			THEME_CITY,
			THEME_AURORA,
			THEME_SUNSET,
			_NUM_THEMES
		};

		// The player's own background pictures: PNG files in this folder on the SD card (free, see "themes/README.txt" there)
		static const int THEME_CUSTOM = 100;		// As "mSelectedTheme", with the file name in "mCustomTheme"

		// Follows the 3DS clock (free): Aurora in the morning, Blue Zig-Zag by day, Sunset Glow in the evening and Starry Night
		// at night - each one only if owned, otherwise one of the others
		static const int THEME_AUTO = 101;

		// Night mode: dims the bottom screen
		enum NightMode
		{
			NIGHT_MODE_OFF = 0,
			NIGHT_MODE_AFTER_DARK,		// From "mNightStart" to "mNightEnd", by default 20:00 to 6:00
			NIGHT_MODE_ALWAYS,
			_NUM_NIGHT_MODES
		};
		static const char* const CUSTOM_THEMES_FOLDER = "sdmc:/3ds/sonic3air/themes/";

		// Starting shield, given at the start of each act and after losing a life (as flags of the game's "char.bonus" byte)
		enum Shield
		{
			SHIELD_OFF = 0,
			SHIELD_CLASSIC,
			SHIELD_FIRE,
			SHIELD_LIGHTNING,
			SHIELD_BUBBLE,
			_NUM_SHIELDS
		};

		struct Data
		{
			uint32_t mOwnedThemes = (1 << THEME_BLUE);
			int mSelectedTheme = THEME_BLUE;
			std::string mCustomTheme;		// File name (UTF-8) in CUSTOM_THEMES_FOLDER, if mSelectedTheme == THEME_CUSTOM
			bool mOwnsShield = false;
			int mSelectedShield = SHIELD_OFF;
			bool mShowMenu = true;		// The "3DS" entry in the main menu (option in the System tab of the options)
			int mNightMode = NIGHT_MODE_OFF;
			int mNightStart = 20 * 60;		// Minutes after midnight, for NIGHT_MODE_AFTER_DARK
			int mNightEnd = 6 * 60;
		};

		Data& get();		// Loaded on first use
		void save();		// Writes the file only if something changed

		// The theme to show right now: for THEME_AUTO, the one for the time of day; any other theme as it is
		int resolveTheme(int theme);

		// Whether the bottom screen should be dimmed right now
		bool isNightModeActive();

		uint8_t getShieldFlags(int shield);		// Flags for the game's "char.bonus" byte, 0 for SHIELD_OFF
	}
}
