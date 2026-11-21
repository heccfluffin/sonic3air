/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: reading and spending the system's Play Coins.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once


namespace n3ds
{
	namespace playcoins
	{
		// Reads the current number of Play Coins. Returns false if they can't be read (nothing gets changed then).
		// "log" false: no log entry on failure, for reading on the background thread.
		bool read(int& outCoins, bool log = true);

		// Spends Play Coins: only if there are enough, and only after the file was checked to be valid. Returns false if nothing
		// was spent. "outCoinsLeft" receives the number of coins afterwards (or the unchanged number if nothing was spent).
		bool spend(int amount, int& outCoinsLeft);
	}
}
