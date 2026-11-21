/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: draws boss sprites straight from the player's ROM, for the journal's postcards.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include <rmxbase.h>


namespace n3ds
{
	namespace romsprites
	{
		struct ArtLoad
		{
			enum class Type : uint8 { NONE, KOSINSKI_MODULED, NEMESIS, PATTERN_LOADING_CUE };
			Type mType = Type::NONE;
			uint32 mAddress = 0;		// In the ROM, or the cue's index
			uint16 mTarget = 0;			// In VRAM
		};

		// What the game itself loads for a boss: its art into VRAM, its palette line, and the object's mappings and tiles
		struct RomSprite
		{
			ArtLoad mLoads[3];
			uint32 mPaletteLine1 = 0;		// Palette line 1 in the ROM (line 0 is the characters' palette)
			uint32 mAttributeTable = 0;		// Mappings address (u32) and sprite attributes (u16) in the ROM
			uint8 mFrame = 0;
		};

		// Draws the sprite into the bitmap (sized to fit it, see-through around it); false if the ROM data doesn't look right
		bool draw(const RomSprite& sprite, Bitmap& outBitmap);
	}
}

#endif
