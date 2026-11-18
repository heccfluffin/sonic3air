/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: loading screen, shown from the moment the game opens until its first frame.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once


namespace n3ds
{
	namespace splash
	{
		// Very early at startup: sets up the graphics itself and shows the loading screen (top) and "Loading..." (bottom)
		void showEarly();

		// Before SDL's video gets initialized (SDL sets up the graphics itself): releases the graphics set up by "showEarly"
		void endEarly();

		// Right after SDL's video is up: shows the loading screen again (via the GPU presenter) until the first game frame
		void showAfterVideoInit();
	}
}
