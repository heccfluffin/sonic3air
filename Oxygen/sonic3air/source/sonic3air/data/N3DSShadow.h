/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the shadow of Doctor Robotnik's "Outrun your shadow" daily challenge - a gray copy of the character
*	with a red trail, who runs the exact same way a few seconds behind. Touching him counts as a hit (or is the end, in the
*	Robotnik's Special version); the insta-shield, a shield's move, Knuckles' glide or invincibility knock him back instead.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include <rmxbase.h>


namespace n3ds
{
	namespace shadow
	{
		static const int START_DELAY = 180;			// Frames behind the character: 3 seconds
		static const int DEADLY_DELAY = 120;		// 2 seconds in the Robotnik's Special version
		static const int KNOCK_BACK_FRAMES = 90;	// Each knock-back puts him 1.5 seconds further behind...
		static const int MAX_DELAY = 600;			// ...up to 10 seconds (then he's only stunned for a moment)

		// Forgets the recorded way, e.g. when the act (re)starts: he appears again after the delay
		void reset();

		// After each frame of the game (also while paused, to draw him): records the character, moves the shadow and draws
		// him - only while "active" (the challenge's act is played), otherwise he's gone
		void onPostFrame(bool active, bool deadly);

		// Before each frame of the game: if he touched the character, the hit happens now (it runs the game's own scripts,
		// which can only be done between frames)
		void onControls();

		// Whether he touched the character since this was last asked (the challenge's try is lost)
		bool takeTouchedNotice();
	}
}

#endif
