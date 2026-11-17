/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: making sure that exiting the game always returns to the HOME Menu.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once


namespace n3ds
{
	namespace exitguard
	{
		// Call when the game starts to exit (after the main loop ended, settings already saved).
		// Starts a watchdog: if the process hasn't ended after the given time (i.e. a shutdown step hangs), it ends the process
		// itself, so the 3DS returns to the HOME Menu instead of staying on a black screen. In that case, it writes the step
		// that hung to "savedata/exit_hang.txt" for debugging.
		void start(unsigned int seconds);

		// Records (and logs) the shutdown step that starts now
		void step(const char* name);
	}
}
