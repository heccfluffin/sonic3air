/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: writing files in the background.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include <functional>
#include <stdint.h>
#include <string>
#include <vector>


namespace n3ds
{
	// The SD card is sometimes slow to respond: a file save that usually takes 50 ms was seen taking 1.9 seconds, and the game
	// froze for that long. Files written through here get written on a background thread instead, while the game goes on.
	namespace filewriter
	{
		// Called on the main thread (in "update") once the file is written; "ms" is how long writing took
		typedef std::function<void(bool success, uint32_t ms)> DoneCallback;

		// Queues a file to be written, with its full path as UTF-8 (like "sdmc:/3ds/sonic3air/savedata/..."). Missing directories
		// get created. A write of the same file that's still waiting in the queue gets replaced (and its callback is not called).
		void writeFile(const std::string& path, std::vector<uint8_t>&& data, DoneCallback onDone = nullptr);

		// Whether any write is waiting or running
		bool isBusy();

		// Blocks until all writes are done; needed before reading or deleting a file that may still be written, and at exit
		void waitUntilDone();

		// Main thread, every frame: logs finished writes (if slow or failed) and calls their callbacks
		void update();
	}
}

#endif
