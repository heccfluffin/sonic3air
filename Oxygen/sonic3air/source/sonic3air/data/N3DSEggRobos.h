/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: Egg Robo patrols - now and then (a setting on the "3DS" menu's Challenge tab) Sky Sanctuary's Egg
*	Robos fly into other zones to hunt you down: they take up a clear spot at your height, facing you, charge their laser
*	gun and fire when nothing solid is in between. They're drawn from the ROM in their own colors, and pop like any badnik
*	when you jump or roll into them. Red Eye watches over each act as a security camera, and when it spots you, Mecha Sonic
*	comes to lead them: he chases you along your own way and takes three hits to beat.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include <rmxbase.h>


namespace n3ds
{
	namespace eggrobos
	{
		// Before each frame of the game: arrivals, movement, shots, and what touches what (hits run the game's own scripts, which
		// can only be done between frames). "allowed": a normal game or Act Select, in a zone they may visit.
		void onControls(bool allowed);

		// After each frame of the game: draws them
		void onPostFrame();

		// Gone at once (like when the act changes)
		void clear();
	}
}

#endif
