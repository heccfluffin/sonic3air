/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: draws bosses and levels straight from the player's ROM, for the journal's postcards.
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
			enum class Type : uint8 { NONE, KOSINSKI_MODULED, NEMESIS, PATTERN_LOADING_CUE, CHARACTER_FRAME };
			Type mType = Type::NONE;
			uint32 mAddress = 0;		// In the ROM, or the cue's index; for CHARACTER_FRAME the table of art pieces per frame
			uint16 mTarget = 0;			// In VRAM; for CHARACTER_FRAME the frame (its art goes to the start of VRAM)
			uint32 mSource = 0;			// CHARACTER_FRAME only: the uncompressed art the pieces are copied from
		};

		// One sprite of a boss: like a child object the game spawns, with its mappings, tiles, frame and offset
		struct SpritePart
		{
			uint32 mMapping = 0;		// 0 ends the list
			uint16 mAttributes = 0;		// Tile, palette line, flags
			uint8 mFrame = 0;
			int16 mX = 0;
			int16 mY = 0;
			uint16 mPriority = 0;		// Higher values are further back; with the same priority, earlier parts are in front
		};

		// What the game loads for a boss (art into VRAM, its palette line 1), and the parts that make it up
		struct RomSprite
		{
			ArtLoad mLoads[3];
			uint32 mPaletteLine1 = 0;
			SpritePart mParts[8];
			uint32 mSuperColors = 0;	// If set: colors 2 to 4 of palette line 0 come from here (like Super Sonic's gold)
		};

		// Draws the whole sprite into the bitmap (sized to fit it, see-through around it); false if the ROM data doesn't look right
		// "outOrigin" (optional) gets where the sprite's own origin is in the bitmap, for lining up frames of different sizes
		bool draw(const RomSprite& sprite, bool flipX, Bitmap& outBitmap, Vec2i* outOrigin = nullptr);

		// A view into a level: its background layer and (optionally) the foreground on top, each at its own position
		struct LevelView
		{
			enum class Setup : uint8 { NONE, AIZ2_BOSS };		// Changes the game makes in some places, like before a boss
			uint8 mZone = 0;
			uint8 mAct = 0;
			Setup mSetup = Setup::NONE;
			int16 mBackgroundX = 0;
			int16 mBackgroundY = 0;
			bool mHasForeground = false;
			int16 mForegroundX = 0;
			int16 mForegroundY = 0;
		};

		bool drawLevel(const LevelView& view, int width, int height, Bitmap& outBitmap);
	}
}

#endif
