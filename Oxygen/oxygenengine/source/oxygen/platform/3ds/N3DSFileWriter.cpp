/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: writing files in the background.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSFileWriter.h"

#include <3ds.h>
#include <map>
#include <deque>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>


namespace n3ds
{
	namespace filewriter
	{
		namespace
		{
			struct Job
			{
				std::string mPath;
				std::vector<uint8_t> mData;
				DoneCallback mOnDone;
				std::function<void()> mWork;		// For "runInBackground" jobs instead of writing a file
				std::function<void()> mOnWorkDone;
				const char* mName = nullptr;		// For the log if it's slow; nullptr to not report it
				bool mIsJob = false;
				size_t mSize = 0;
				bool mSuccess = false;
				bool mReplaced = false;		// Never written, a newer write of the same file took its place
				uint64_t mNotBefore = 0;	// System tick: a file waits a moment, so a few quick changes become one write
				uint32_t mMs = 0;
			};

			LightLock gLock;
			LightEvent gWakeEvent;
			Thread gThread = nullptr;
			bool gInitialized = false;

			// All guarded by "gLock"
			std::deque<Job> gQueue;
			std::vector<Job> gFinished;
			bool gWriting = false;
			bool gFlush = false;		// Write everything right away (when waiting for it)

			const uint64_t WRITE_DELAY_TICKS = SYSCLOCK_ARM11;		// 1 second (for small files)

			uint32_t ticksToMs(uint64_t ticks)
			{
				return (uint32_t)(ticks * 1000 / SYSCLOCK_ARM11);
			}

			void createParentDirectories(const std::string& path)
			{
				// Each directory level after the device name ("sdmc:/"); errors are ignored, mostly they exist already
				const size_t start = path.find(":/");
				for (size_t pos = path.find('/', (start == std::string::npos) ? 1 : start + 2); pos != std::string::npos; pos = path.find('/', pos + 1))
				{
					mkdir(path.substr(0, pos).c_str(), 0777);
				}
			}

			const char* const TEMP_SUFFIX = ".new";

			bool writeNow(const std::string& path, const std::vector<uint8_t>& data)
			{
				// A small file that's there already (like the challenges or the journal): written over in place. On a slow SD card
				// each step on its directory takes long (creating a file, removing one, renaming); this way there's none of them,
				// and it's still safe: no more than one sector (512 bytes) gets written, which happens all at once or not at all.
				if (data.size() <= 512)
				{
					const int fd = open(path.c_str(), O_WRONLY);
					if (fd >= 0)
					{
						const ssize_t result = data.empty() ? 0 : write(fd, data.data(), data.size());
						bool success = (result == (ssize_t)data.size());
						if (success && ftruncate(fd, (off_t)data.size()) != 0)
							success = false;
						if (close(fd) != 0)
							success = false;
						if (success)
							return true;
						// Otherwise the safe way below, which writes all of it again
					}
				}

				// Written under another name first, then put in place: if the 3DS gets switched off or the game crashes while the
				// SD card is still writing (which can take seconds), the old file stays complete instead of being cut off -
				// for the game's save data, the journal or a save state, that would lose all of it
				const std::string tempPath = path + TEMP_SUFFIX;
				int fd = open(tempPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
				if (fd < 0)
				{
					// Only if opening fails: the directories might be missing (checking them each time costs time on a slow SD card)
					createParentDirectories(path);
					fd = open(tempPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
					if (fd < 0)
						return false;
				}

				bool success = true;
				size_t written = 0;
				while (written < data.size())
				{
					const ssize_t result = write(fd, data.data() + written, data.size() - written);
					if (result <= 0)
					{
						success = false;
						break;
					}
					written += (size_t)result;
				}
				if (close(fd) != 0)
					success = false;
				if (!success)
				{
					unlink(tempPath.c_str());		// The old file is still there, untouched
					return false;
				}

				// The SD card's file system can't rename onto an existing file, so the old one goes first. Should the power go off
				// right in between, the next start finds the complete new file without the old one and puts it in place.
				unlink(path.c_str());
				return (rename(tempPath.c_str(), path.c_str()) == 0);
			}

			void threadFunc(void*)
			{
				while (true)
				{
					LightEvent_Wait(&gWakeEvent);
					while (true)
					{
						LightLock_Lock(&gLock);
						if (gQueue.empty())
						{
							LightLock_Unlock(&gLock);
							break;
						}

						// The first one that's ready (a file a second after its latest change, background work right away)
						const uint64_t now = svcGetSystemTick();
						auto ready = gQueue.end();
						uint64_t nextReady = UINT64_MAX;
						for (auto it = gQueue.begin(); it != gQueue.end(); ++it)
						{
							if (gFlush || it->mNotBefore <= now)
							{
								ready = it;
								break;
							}
							nextReady = std::min(nextReady, it->mNotBefore);
						}
						if (ready == gQueue.end())
						{
							LightLock_Unlock(&gLock);
							LightEvent_WaitTimeout(&gWakeEvent, (int64_t)((nextReady - now) * 1000000000ull / SYSCLOCK_ARM11) + 1000000);
							continue;
						}
						Job job = std::move(*ready);
						gQueue.erase(ready);
						gWriting = true;
						LightLock_Unlock(&gLock);

						const uint64_t startTicks = svcGetSystemTick();
						if (job.mWork)
						{
							job.mWork();
							job.mWork = nullptr;		// Release what it captured here already
							job.mSuccess = true;
						}
						else
						{
							job.mSuccess = writeNow(job.mPath, job.mData);
						}
						job.mMs = ticksToMs(svcGetSystemTick() - startTicks);
						job.mSize = job.mData.size();
						std::vector<uint8_t>().swap(job.mData);		// The data isn't needed any more

						LightLock_Lock(&gLock);
						gFinished.push_back(std::move(job));
						gWriting = false;
						LightLock_Unlock(&gLock);
					}
				}
			}

			bool startThread()
			{
				if (!gInitialized)
				{
					gInitialized = true;
					LightLock_Init(&gLock);
					LightEvent_Init(&gWakeEvent, RESET_ONESHOT);

					// On the system core (core 1), of which the game may use 30% (see APT_SetAppCpuTimeLimit in PlatformSpecifics.cpp):
					// there it doesn't have to wait for the main thread to be idle, which in slower menus it hardly ever is.
					// If that's not possible, on the game's own core with slightly lower priority than the main thread.
					// Detached, it runs until the process ends.
					s32 mainPriority = 0x30;
					svcGetThreadPriority(&mainPriority, CUR_THREAD_HANDLE);
					const s32 priority = std::min<s32>(mainPriority + 1, 0x3f);
					gThread = threadCreate(threadFunc, nullptr, 0x8000, priority, 1, true);
					if (nullptr != gThread)
					{
						RMX_LOG_INFO("File writer: background thread runs on the system core");
					}
					else
					{
						gThread = threadCreate(threadFunc, nullptr, 0x8000, priority, -2, true);
						if (nullptr != gThread)
							RMX_LOG_INFO("File writer: background thread runs on the game's core (the system core was not available)");
					}
					if (nullptr == gThread)
						RMX_LOG_INFO("File writer: could not start the background thread, writing files directly instead");
				}
				return (nullptr != gThread);
			}
		}

		void recoverInterruptedWrites(const std::string& directory)
		{
			// See "writeNow": a new version next to its file means the writing got interrupted. If the file itself is still there, the
			// new version may be incomplete: it goes, the old file stays. If the file is missing, the new version is complete (the old
			// file only gets removed after that): it takes the file's place.
			DIR* dir = opendir(directory.c_str());
			if (nullptr == dir)
				return;
			std::vector<std::string> found;
			const size_t suffixLength = strlen(TEMP_SUFFIX);
			while (const dirent* entry = readdir(dir))
			{
				const size_t length = strlen(entry->d_name);
				if (length > suffixLength && strcmp(entry->d_name + length - suffixLength, TEMP_SUFFIX) == 0)
					found.emplace_back(entry->d_name);
			}
			closedir(dir);

			for (const std::string& name : found)
			{
				const std::string tempPath = directory + name;
				const std::string path = tempPath.substr(0, tempPath.length() - suffixLength);
				struct stat info;
				if (stat(path.c_str(), &info) == 0)
				{
					unlink(tempPath.c_str());
					RMX_LOG_INFO("File writer: removed an unfinished write, the previous version stays: " << path);
				}
				else if (rename(tempPath.c_str(), path.c_str()) == 0)
				{
					RMX_LOG_INFO("File writer: finished an interrupted write: " << path);
				}
			}
		}

		void writeFile(const std::string& path, std::vector<uint8_t>&& data, DoneCallback onDone)
		{
			if (!startThread())
			{
				// Fallback: write directly
				const uint64_t startTicks = svcGetSystemTick();
				const bool success = writeNow(path, data);
				const uint32_t ms = ticksToMs(svcGetSystemTick() - startTicks);
				if (!success)
					RMX_LOG_INFO("File writer: writing FAILED: " << path);
				if (onDone)
					onDone(success, ms);
				return;
			}

			LightLock_Lock(&gLock);
			for (Job& queued : gQueue)
			{
				if (!queued.mIsJob && queued.mPath == path)
				{
					// Not started yet: write the new data instead (only the new write's callback gets called)
					queued.mReplaced = true;
					gFinished.push_back(std::move(queued));
					queued = Job();
					queued.mPath = path;
					queued.mData = std::move(data);
					queued.mOnDone = std::move(onDone);
					if (queued.mData.size() <= 0x4000)
						queued.mNotBefore = svcGetSystemTick() + WRITE_DELAY_TICKS;
					LightLock_Unlock(&gLock);
					LightEvent_Signal(&gWakeEvent);
					return;
				}
			}
			Job& job = gQueue.emplace_back();
			job.mPath = path;
			job.mData = std::move(data);
			job.mOnDone = std::move(onDone);
			if (job.mData.size() <= 0x4000)
				job.mNotBefore = svcGetSystemTick() + WRITE_DELAY_TICKS;		// Small files (settings, challenges...), not save states
			LightLock_Unlock(&gLock);
			LightEvent_Signal(&gWakeEvent);
		}

		void runInBackground(const char* name, std::function<void()> work, std::function<void()> onDone)
		{
			if (!startThread())
			{
				// Fallback: right here
				work();
				if (onDone)
					onDone();
				return;
			}
			LightLock_Lock(&gLock);
			Job& job = gQueue.emplace_back();
			job.mName = name;
			job.mIsJob = true;
			job.mWork = std::move(work);
			job.mOnWorkDone = std::move(onDone);
			LightLock_Unlock(&gLock);
			LightEvent_Signal(&gWakeEvent);
		}

		bool writeFileIfOnSDCard(const std::wstring& path, std::vector<uint8_t>& data, DoneCallback onDone)
		{
			if (path.compare(0, 6, L"sdmc:/") != 0)
				return false;

			// Nothing to do if the file got exactly this content already (like settings saved without a change): every write keeps
			// the SD card busy for a moment, and anything reading from it meanwhile has to wait
			static std::map<std::wstring, uint64_t> lastWritten;
			uint64_t hash = 0xcbf29ce484222325ull ^ data.size();
			for (const uint8_t byte : data)
				hash = (hash ^ byte) * 0x100000001b3ull;
			const auto it = lastWritten.find(path);
			if (it != lastWritten.end() && it->second == hash)
			{
				if (onDone)
					onDone(true, 0);
				return true;
			}
			lastWritten[path] = hash;

			writeFile(rmx::convertToUTF8(path), std::move(data), std::move(onDone));
			return true;
		}

		bool isBusy()
		{
			if (nullptr == gThread)
				return false;
			LightLock_Lock(&gLock);
			const bool busy = gWriting || !gQueue.empty();
			LightLock_Unlock(&gLock);
			return busy;
		}

		void waitUntilDone()
		{
			if (isBusy())
			{
				const uint64_t startTicks = svcGetSystemTick();
				LightLock_Lock(&gLock);
				gFlush = true;
				LightLock_Unlock(&gLock);
				LightEvent_Signal(&gWakeEvent);
				while (isBusy())
				{
					svcSleepThread(5 * 1000 * 1000);		// 5 ms
				}
				LightLock_Lock(&gLock);
				gFlush = false;
				LightLock_Unlock(&gLock);
				RMX_LOG_INFO("File writer: waited " << ticksToMs(svcGetSystemTick() - startTicks) << " ms for files to be written");
			}
			update();
		}

		void update()
		{
			if (nullptr == gThread)
				return;

			std::vector<Job> finished;
			LightLock_Lock(&gLock);
			finished.swap(gFinished);
			LightLock_Unlock(&gLock);

			for (Job& job : finished)
			{
				if (job.mIsJob)
				{
					// A "runInBackground" job
					if (nullptr != job.mName && job.mMs >= 200)
						RMX_LOG_INFO("Background job: " << job.mName << " took " << job.mMs << " ms (the SD card or system storage was slow, but the game went on)");
					if (job.mOnWorkDone)
						job.mOnWorkDone();
					continue;
				}
				if (job.mReplaced)
				{
					// Replaced by a newer write of the same file before it started
				}
				else if (!job.mSuccess)
				{
					RMX_LOG_INFO("File writer: writing FAILED after " << job.mMs << " ms: " << job.mPath);
				}
				else if (job.mMs >= 200)
				{
					RMX_LOG_INFO("File writer: slow SD card, " << job.mMs << " ms in the background for " << job.mSize << " bytes: " << job.mPath);
				}

				if (job.mOnDone && !job.mReplaced)
					job.mOnDone(job.mSuccess, job.mMs);
			}
		}
	}
}

#endif
