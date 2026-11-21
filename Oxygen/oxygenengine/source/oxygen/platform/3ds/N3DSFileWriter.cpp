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
#include <deque>
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
				const char* mName = nullptr;
				size_t mSize = 0;
				bool mSuccess = false;
				bool mReplaced = false;		// Never written, a newer write of the same file took its place
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

			bool writeNow(const std::string& path, const std::vector<uint8_t>& data)
			{
				int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
				if (fd < 0)
				{
					// Only if opening fails: the directories might be missing (checking them each time costs time on a slow SD card)
					createParentDirectories(path);
					fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
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
				return success;
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
						Job job = std::move(gQueue.front());
						gQueue.pop_front();
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
				if (!queued.mWork && queued.mPath == path)
				{
					// Not started yet: write the new data instead (only the new write's callback gets called)
					queued.mReplaced = true;
					gFinished.push_back(std::move(queued));
					queued = Job();
					queued.mPath = path;
					queued.mData = std::move(data);
					queued.mOnDone = std::move(onDone);
					LightLock_Unlock(&gLock);
					LightEvent_Signal(&gWakeEvent);
					return;
				}
			}
			Job& job = gQueue.emplace_back();
			job.mPath = path;
			job.mData = std::move(data);
			job.mOnDone = std::move(onDone);
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
			job.mWork = std::move(work);
			job.mOnWorkDone = std::move(onDone);
			LightLock_Unlock(&gLock);
			LightEvent_Signal(&gWakeEvent);
		}

		bool writeFileIfOnSDCard(const std::wstring& path, std::vector<uint8_t>& data, DoneCallback onDone)
		{
			if (path.compare(0, 6, L"sdmc:/") != 0)
				return false;
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
				while (isBusy())
				{
					svcSleepThread(5 * 1000 * 1000);		// 5 ms
				}
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
				if (nullptr != job.mName)
				{
					// A "runInBackground" job
					if (job.mMs >= 200)
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
