/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: writing the log file in the background.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSLog.h"
#include "oxygen/platform/3ds/N3DSFileWriter.h"

#include <3ds.h>
#include <malloc.h>
#include <stdio.h>

extern "C" u32 __ctru_linear_heap_size;		// From libctru, like in rmxbase's Logging.cpp


namespace rmx
{
	namespace detail
	{
		std::string getTimestampString();		// In rmxbase's Logging.cpp, for the same timestamps as rmx::FileLogger
	}
}


namespace n3ds
{
	namespace log
	{
		namespace
		{
			FILE* gFile = nullptr;				// Used by the background thread, or by the main thread once synchronous
			bool gSynchronous = false;

			LightLock gPendingLock;
			std::string gPending;				// Lines not written yet (guarded by gPendingLock)
			bool gFlushQueued = false;			// Whether a background job for writing them is queued (guarded by gPendingLock)

			void writeToFile(const std::string& text)
			{
				if (nullptr != gFile && !text.empty())
				{
					fwrite(text.data(), 1, text.length(), gFile);
					fflush(gFile);
				}
			}

			void flushPending()
			{
				// Background thread (or main thread, once synchronous)
				LightLock_Lock(&gPendingLock);
				std::string text;
				text.swap(gPending);
				gFlushQueued = false;
				LightLock_Unlock(&gPendingLock);
				writeToFile(text);
			}

			class AsyncFileLogger final : public rmx::LoggerBase
			{
			protected:
				void log(rmx::LogLevel logLevel, const std::string& string) override
				{
					// Same format as rmx::FileLogger: timestamp, memory in use, the message
					std::string line = rmx::detail::getTimestampString();
					{
						const struct mallinfo info = mallinfo();
						const u32 linearUsed = __ctru_linear_heap_size - linearSpaceFree();
						char memoryString[64];
						const int length = snprintf(memoryString, sizeof(memoryString), "[heap %.1f, arena %.1f, linear %.1f MB] ",
													(double)info.uordblks / 1048576.0, (double)info.arena / 1048576.0, (double)linearUsed / 1048576.0);
						if (length > 0 && length < (int)sizeof(memoryString))
							line.append(memoryString, (size_t)length);
					}
					line += string;
					line += "\r\n";

					if (gSynchronous)
					{
						writeToFile(line);
						return;
					}

					LightLock_Lock(&gPendingLock);
					gPending += line;
					const bool queueJob = !gFlushQueued;
					gFlushQueued = true;
					LightLock_Unlock(&gPendingLock);

					// One job takes all lines that came in until it runs. Not reported if slow: that report would be a log line
					// itself, queueing another slow write, and so on while the SD card is slow.
					if (queueJob)
						n3ds::filewriter::runInBackground(nullptr, &flushPending, nullptr);
				}
			};
		}

		void addAsyncFileLogger(const std::wstring& filename)
		{
			LightLock_Init(&gPendingLock);

			// Create the directory if needed, like rmx::FileLogger
			const size_t slashPosition = filename.find_last_of(L"/\\");
			if (slashPosition != std::wstring::npos)
				FTX::FileSystem->createDirectory(filename.substr(0, slashPosition));

			gFile = fopen(rmx::convertToUTF8(filename).c_str(), "wb");
			rmx::Logging::addLogger(*new AsyncFileLogger());
		}

		void setSynchronous()
		{
			if (gSynchronous)
				return;
			gSynchronous = true;
			n3ds::filewriter::waitUntilDone();
			flushPending();		// Anything still left (e.g. if the background thread could not be started)
		}
	}
}

#endif
