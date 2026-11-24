/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the shadow ambush - now and then (1% of acts by default, set in the "3DS" menu's Challenge tab), the
*	shadow from Doctor Robotnik's challenges turns up uninvited: a WARNING with an alarm, then the Doomsday music, and he
*	chases you for the rest of the act. Giant rings and bonus stage stars stay out of reach while he's there. Reaching the
*	end of the act is worth a Robotnik voucher.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include <rmxbase.h>

class Drawer;


namespace n3ds
{
	namespace ambush
	{
		// Before each frame of the game. "challengeMode": a normal game or Act Select (otherwise everything is forgotten, like
		// back in the menus); "levelRunning": an act is being played; "allowedHere": an act he may turn up in (not a bonus
		// stage, not Doomsday, not the act of a shadow challenge). A new act gets its roll of the dice.
		void onControls(bool challengeMode, bool levelRunning, uint16 zoneAct, bool allowedHere);

		// Drawn over the game (by GameApp): the WARNING, and the message at the end
		void render(Drawer& drawer, int width);

		bool isActive();		// He's coming (from the warning on)
		bool isChasing();		// The warning is over: he's after you

		// The act was finished: true if he was there (and so got outrun)
		bool onActCompleted();
	}
}

#endif
