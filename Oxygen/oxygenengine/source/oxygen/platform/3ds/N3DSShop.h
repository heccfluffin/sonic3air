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
			_NUM_THEMES
		};

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
			bool mOwnsShield = false;
			int mSelectedShield = SHIELD_OFF;
			bool mShowMenu = true;		// The "3DS" entry in the main menu (option in the System tab of the options)
		};

		Data& get();		// Loaded on first use
		void save();		// Writes the file only if something changed

		uint8_t getShieldFlags(int shield);		// Flags for the game's "char.bonus" byte, 0 for SHIELD_OFF
	}
}
