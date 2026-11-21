/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: writing the log file in the background.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include <string>


namespace n3ds
{
	// Each log line used to be written and flushed to the SD card right away, on the thread that logged it - mostly the main
	// thread. While the SD card was busy with a slow write (seen taking 1.8 seconds for a few bytes), that froze the game.
	// This logger formats each line right away (same format as rmx::FileLogger, with the memory in use), and the file writer's
	// background thread appends them to the file, in order with the other writes.
	namespace log
	{
		// Creates the log file (replacing an existing one) and adds the logger to rmx::Logging
		void addAsyncFileLogger(const std::wstring& filename);

		// From now on write each line directly (for the exit, so its last steps are on the SD card for sure);
		// finishes all pending background writes first
		void setSynchronous();
	}
}

#endif
