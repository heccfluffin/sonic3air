/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once


namespace n3ds
{
	// Shows the controls overview image on the 3DS bottom screen (which the game itself doesn't use).
	// Call after SDL video is set up; it also redraws itself when returning from the HOME menu.
	void drawControlsScreen();
}
