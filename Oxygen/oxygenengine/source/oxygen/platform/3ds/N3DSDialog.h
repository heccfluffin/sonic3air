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
	// Shows a text dialog directly on a 3DS screen (works before and after SDL video is set up)
	//  - numButtons 1: [A] OK
	//  - numButtons 2: [A] OK, [B] Cancel
	//  - numButtons 3: [A] Yes, [X] No, [B] Cancel
	// Returns 0 for OK / Yes, 1 for No, 2 for Cancel
	int showDialog(const char* caption, const char* text, int numButtons);
}
