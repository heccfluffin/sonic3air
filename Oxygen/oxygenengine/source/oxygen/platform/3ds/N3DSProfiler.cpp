/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: lightweight frame profiler.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS) && defined(N3DS_PROFILER)

#include "oxygen/platform/3ds/N3DSProfiler.h"
#include "oxygen/simulation/EmulatorInterface.h"

#include <3ds.h>
#include <atomic>
#include <map>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <vector>

#ifndef S3AIR_3DS_BUILD_ID
	#define S3AIR_3DS_BUILD_ID "local"
#endif


namespace n3ds
{
	namespace profiler
	{
		namespace
		{
			const constexpr double TICKS_PER_MS = (double)SYSCLOCK_ARM11 / 1000.0;
			const constexpr uint64_t REPORT_INTERVAL_TICKS = (uint64_t)SYSCLOCK_ARM11 * 2;		// Every 2 seconds
			const constexpr double SCREEN_REFRESH_RATE = 59.831;								// Refresh rate of the 3DS screens
			const constexpr uint64_t REFRESH_TICKS = (uint64_t)((double)SYSCLOCK_ARM11 / SCREEN_REFRESH_RATE);

			// Bottom screen console: 40 x 30 characters; lines get padded to 39 characters, as a full line would wrap
			const constexpr int SCREEN_LINES = 30;
			const constexpr int SCREEN_LINE_LENGTH = 39;
			const constexpr int SCREEN_LINES_PER_FRAME = 3;		// The table gets printed over several frames, to keep the cost per frame low

			// Current frame
			uint64_t gSectionStart[_NUM] = { 0 };
			uint64_t gFrameAccum[_NUM] = { 0 };
			uint32_t gFrameSimSteps = 0;
			uint32_t gFrameCounts[_NUM_COUNTERS] = { 0 };
			uint64_t gWindowCounts[_NUM_COUNTERS] = { 0 };
			uint32_t gFrameSectionCalls = 0;

			// Current report window
			uint64_t gWindowTotal[_NUM] = { 0 };
			uint64_t gWindowMax[_NUM] = { 0 };
			uint64_t gWindowFrameTotal = 0;
			uint64_t gWindowFrameMax = 0;
			uint64_t gWindowOtherTotal = 0;
			uint64_t gWindowOtherMax = 0;
			uint32_t gWindowFrames = 0;
			uint32_t gWindowSimSteps = 0;
			uint32_t gWindowSimStepsMax = 0;
			uint32_t gWindowMissedRefreshes = 0;
			uint64_t gWindowSectionCalls = 0;
			uint64_t gWindowStart = 0;
			uint64_t gLastFrameEnd = 0;

			float gTargetStepRate = 60.0f;
			double gTicksPerSectionCall = 0.0;		// Measured cost of one begin/end pair, see "calibrate"

			std::atomic<uint32_t> gWorkerTicks { 0 };

			PrintConsole gConsole;
			bool gConsoleInitialized = false;
			char gScreenLines[SCREEN_LINES][SCREEN_LINE_LENGTH + 1] = { { 0 } };
			int gNextScreenLine = SCREEN_LINES;		// Next line to print to the bottom screen, SCREEN_LINES if all done

			// Top-level sections, i.e. the ones that don't overlap each other
			const Section TOP_LEVEL_SECTIONS[] = { SIMULATION, UI_UPDATE, AUDIO_MAIN, RENDER, PRESENT_REV, PRESENT_SDL, WAIT, PROFILER };

			struct Line
			{
				const char* mLabel;
				int mSection;		// -1 for "Frame total", -2 for "Unaccounted"
				bool mOnScreen;		// Otherwise only in the log file
			};
			const Line LINES[] =
			{
				{ "Frame total",        -1,           true },
				{ " Game logic",        SIMULATION,   true },
				{ "   Scripts",         SCRIPTS,      true },
				{ " Menus/UI update",   UI_UPDATE,    true },
				{ " Audio (main)",      AUDIO_MAIN,   true },
				{ " Render (CPU)",      RENDER,       true },
				{ "   VDP refresh",     REFRESH,      true },
				{ "   Geometry",        GEOMETRY,     true },
				{ "   SW game screen",  SW_RENDER,    true },
				{ "     Planes",        SW_PLANES,    true },
				{ "     Sprites",       SW_SPRITES,   true },
				{ "       VDP sprites", SW_SPR_VDP,   false },	// Log & report only (bottom screen is full)
				{ "       Palette spr", SW_SPR_PALETTE, false },
				{ "       Component spr", SW_SPR_COMPONENT, false },
				{ "     Blur",          SW_BLUR,      true },
				{ "   Drawer/UI",       DRAWER,       true },
				{ "     Text",          DRAW_TEXT,    true },
				{ "     Sprites",       DRAW_SPRITE,  true },
				{ "     Rects",         DRAW_RECT,    true },
				{ "     Upscale",       DRAW_UPSCALE, true },
				{ "     Other",         DRAW_OTHER,   true },
				{ "   Menu bg blur",    STILL_BLUR,   true },
				{ " Present: bswap",    PRESENT_REV,  false },	// Only used without the GPU presenter
				{ " Present (total)",   PRESENT_SDL,  true },
				{ "   CPU copy",        PRESENT_COPY, true },
				{ "   GPU upload",      PRESENT_UPLOAD, true },
				{ "   GPU wait",        PRESENT_GPU_WAIT, true },
				{ "   Swap wait",       PRESENT_SWAP_WAIT, true },
				{ " Frame cap wait",    WAIT,         true },
				{ " Profiler",          PROFILER,     false },	// On screen in the last line
				{ " Unaccounted",       -2,           true },
			};

			inline double toMs(uint64_t ticks)	{ return (double)ticks / TICKS_PER_MS; }

			// --- Current area of the game, for telling apart profiler results ---
			//  -> Read from the emulated game memory: game mode at 0xfffff600 and zone/act at 0xfffffe10 (see scripts/_defines/defines_global.lemon)

			struct ZoneName { const char* mShort; const char* mLong; };
			const ZoneName ZONE_NAMES[] =
			{
				{ "AIZ", "Angel Island" },		{ "HCZ", "Hydrocity" },			{ "MGZ", "Marble Garden" },		{ "CNZ", "Carnival Night" },
				{ "FBZ", "Flying Battery" },	{ "ICZ", "IceCap" },			{ "LBZ", "Launch Base" },		{ "MHZ", "Mushroom Hill" },
				{ "SOZ", "Sandopolis" },		{ "LRZ", "Lava Reef" },			{ "SSZ", "Sky Sanctuary" },		{ "DEZ", "Death Egg" },
				{ "DDZ", "The Doomsday" },		{ "BG", "Menu background / ending" },			{ "ALZ", "Azure Lake" },		{ "BPZ", "Balloon Park" },
				{ "DPZ", "Desert Palace" },		{ "CGZ", "Chrome Gadget" },		{ "EMZ", "Endless Mine" },		{ "GUM", "Gumball bonus stage" },
				{ "PAC", "Glowing Spheres bonus stage" },	{ "SLT", "Slot Machine bonus stage" },
			};

			char gAreaShort[12] = "-";
			char gAreaLong[48] = "starting up";
			uint32_t gLastAreaKey = 0xffffffff;
			uint32_t gCandidateAreaKey = 0xffffffff;		// An area change only counts once the new area stayed the same for a while
			int gCandidateAreaFrames = 0;
			const constexpr int AREA_CHANGE_FRAMES = 60;		// About a second

			// Totals for the current visit of the current area, written as one summary line when leaving it
			struct AreaTotals
			{
				uint64_t mSection[_NUM] = { 0 };
				uint64_t mCounts[_NUM_COUNTERS] = { 0 };
				uint64_t mFrameTicks = 0;
				uint64_t mFrameMax = 0;
				uint32_t mFrames = 0;
				uint32_t mSimSteps = 0;
				uint32_t mMissedRefreshes = 0;
			};
			AreaTotals gArea;
			float gAreaTargetStepRate = 60.0f;

			// All visits of each area in this session, for the report file
			struct AreaAggregate
			{
				AreaTotals mTotals;
				uint32_t mVisits = 0;
			};
			std::map<std::string, AreaAggregate> gAreaAggregates;

			void addVisitToAggregate()
			{
				if (gArea.mFrames == 0)
					return;
				AreaAggregate& aggregate = gAreaAggregates[gAreaLong];
				AreaTotals& totals = aggregate.mTotals;
				for (int i = 0; i < _NUM; ++i)
					totals.mSection[i] += gArea.mSection[i];
				for (int i = 0; i < _NUM_COUNTERS; ++i)
					totals.mCounts[i] += gArea.mCounts[i];
				totals.mFrameTicks += gArea.mFrameTicks;
				totals.mFrameMax = std::max(totals.mFrameMax, gArea.mFrameMax);
				totals.mFrames += gArea.mFrames;
				totals.mSimSteps += gArea.mSimSteps;
				totals.mMissedRefreshes += gArea.mMissedRefreshes;
				++aggregate.mVisits;
			}

			// --- Background file writer ---
			//  -> Writing to the SD card can take a long time on the 3DS (up to a second or more), so the report never gets written
			//     on the main thread during the game: a low-priority thread does it, which only runs while the main thread waits anyway

			const char* REPORT_PATH = "sdmc:/3ds/sonic3air/savedata/profile_report.txt";
			const char* PROFILE_LOG_PATH = "sdmc:/3ds/sonic3air/savedata/profile_log.txt";
			const char* PROFILE_LOG_PREVIOUS_PATH = "sdmc:/3ds/sonic3air/savedata/profile_log_previous.txt";
			LightLock gWriterLock;
			LightEvent gWriterEvent;
			Thread gWriterThread = nullptr;
			std::string gPendingReport;		// Protected by gWriterLock
			bool gHasPendingReport = false;
			std::string gPendingLogLines;	// Protected by gWriterLock

			void appendToFile(const char* path, const std::string& text)
			{
				FILE* file = fopen(path, "a");
				if (nullptr != file)
				{
					fwrite(text.data(), 1, text.size(), file);
					fclose(file);
				}
			}

			void writeFile(const char* path, const std::string& text)
			{
				FILE* file = fopen(path, "w");
				if (nullptr != file)
				{
					fwrite(text.data(), 1, text.size(), file);
					fclose(file);
				}
			}

			void writerThreadFunc(void*)
			{
				while (true)
				{
					LightEvent_Wait(&gWriterEvent);
					std::string text;
					std::string logLines;
					LightLock_Lock(&gWriterLock);
					const bool hasReport = gHasPendingReport;
					if (hasReport)
					{
						text.swap(gPendingReport);
						gHasPendingReport = false;
					}
					logLines.swap(gPendingLogLines);
					LightLock_Unlock(&gWriterLock);
					if (!logLines.empty())
						appendToFile(PROFILE_LOG_PATH, logLines);
					if (hasReport)
						writeFile(REPORT_PATH, text);
				}
			}

			void appendFormat(std::string& output, const char* format, ...)
			{
				char buffer[256];
				va_list args;
				va_start(args, format);
				vsnprintf(buffer, sizeof(buffer), format, args);
				va_end(args);
				output += buffer;
			}

			// Writes "profile_report.txt": one row per zone / act / stage, blank for areas not visited in this session
			void writeReport(bool synchronous)
			{
				std::vector<std::string> rows = { "SEGA screen / A.I.R. menus", "Title screen / menu background", "Level start / title card" };
				char name[64];
				for (int zone = 0; zone <= 0x0b; ++zone)
				{
					for (int act = 1; act <= 2; ++act)
					{
						snprintf(name, sizeof(name), "%s Act %d", ZONE_NAMES[zone].mLong, act);
						rows.emplace_back(name);
					}
				}
				snprintf(name, sizeof(name), "%s Act 1", ZONE_NAMES[0x0c].mLong);
				rows.emplace_back(name);
				for (const char* extra : { "Special stage", "After special stage (mode 0x48)", "Blue Sphere menu", "Blue Sphere results", "Gumball bonus stage", "Glowing Spheres bonus stage", "Slot Machine bonus stage" })
					rows.emplace_back(extra);
				for (const auto& pair : gAreaAggregates)	// Anything else that was visited (e.g. competition zones)
				{
					if (std::find(rows.begin(), rows.end(), pair.first) == rows.end())
						rows.push_back(pair.first);
				}

				std::string text;
				text.reserve(6000);
				appendFormat(text, "Sonic 3 A.I.R. 3DS profiling report - build %s\n", S3AIR_3DS_BUILD_ID);
				appendFormat(text, "All visits of each area in this session combined; empty rows: not visited. Times in ms per frame.\n\n");
				appendFormat(text, "%-32s %7s %6s %6s %7s %6s %6s %6s %6s %6s %6s %7s  %s\n",
						"Area", "Time s", "FPS", "Missed", "Speed%", "Frame", "Max", "Logic", "Render", "Planes", "Sprite", "Present",
						"Sprites: VDP ms (count) / palette ms (count) / component ms (count)");
				for (const std::string& row : rows)
				{
					const auto it = gAreaAggregates.find(row);
					if (it == gAreaAggregates.end())
					{
						appendFormat(text, "%s\n", row.c_str());
						continue;
					}
					const AreaTotals& totals = it->second.mTotals;
					const double seconds = (double)totals.mFrameTicks / (double)SYSCLOCK_ARM11;
					const double frames = (totals.mFrames > 0) ? (double)totals.mFrames : 1.0;
					const auto avg = [&](Section section)  { return (double)totals.mSection[section] / TICKS_PER_MS / frames; };
					const double speed = (seconds > 0.0 && gAreaTargetStepRate > 0.0f) ? (double)totals.mSimSteps / seconds * 100.0 / (double)gAreaTargetStepRate : 0.0;
					const auto count = [&](Counter counter)  { return (double)totals.mCounts[counter] / frames; };
					appendFormat(text, "%-32s %7.1f %6.1f %6u %7.1f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %7.2f  %.2f (%.0f) / %.2f (%.0f) / %.2f (%.0f)\n",
							row.c_str(), seconds, (seconds > 0.0) ? frames / seconds : 0.0, (unsigned)totals.mMissedRefreshes, speed,
							(double)totals.mFrameTicks / TICKS_PER_MS / frames, (double)totals.mFrameMax / TICKS_PER_MS,
							avg(SIMULATION), avg(RENDER), avg(SW_PLANES), avg(SW_SPRITES), avg(PRESENT_SDL),
							avg(SW_SPR_VDP), count(COUNT_SPR_VDP), avg(SW_SPR_PALETTE), count(COUNT_SPR_PALETTE), avg(SW_SPR_COMPONENT), count(COUNT_SPR_COMPONENT));
				}

				if (synchronous || nullptr == gWriterThread)
				{
					writeFile(REPORT_PATH, text);
					return;
				}
				LightLock_Lock(&gWriterLock);
				gPendingReport.swap(text);
				gHasPendingReport = true;
				LightLock_Unlock(&gWriterLock);
				LightEvent_Signal(&gWriterEvent);
			}

			// Adds a line to "profile_log.txt" (written in the background, see above)
			void logLine(const std::string& line)
			{
				char timestamp[32] = "";
				const time_t now = time(nullptr);
				const struct tm* local = localtime(&now);
				if (nullptr != local)
					strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S] ", local);

				if (nullptr == gWriterThread)
				{
					appendToFile(PROFILE_LOG_PATH, timestamp + line + "\n");
					return;
				}
				LightLock_Lock(&gWriterLock);
				gPendingLogLines += timestamp;
				gPendingLogLines += line;
				gPendingLogLines += '\n';
				LightLock_Unlock(&gWriterLock);
				LightEvent_Signal(&gWriterEvent);
			}

			void flushLogLinesNow()
			{
				std::string logLines;
				LightLock_Lock(&gWriterLock);
				logLines.swap(gPendingLogLines);
				LightLock_Unlock(&gWriterLock);
				if (!logLines.empty())
					appendToFile(PROFILE_LOG_PATH, logLines);
			}

			void startWriterThread()
			{
				// Keep the previous session's profile log, start a new one
				remove(PROFILE_LOG_PREVIOUS_PATH);
				rename(PROFILE_LOG_PATH, PROFILE_LOG_PREVIOUS_PATH);
				appendToFile(PROFILE_LOG_PATH, std::string("Sonic 3 A.I.R. 3DS profile log - build ") + S3AIR_3DS_BUILD_ID + "\n");

				LightLock_Init(&gWriterLock);
				LightEvent_Init(&gWriterEvent, RESET_ONESHOT);
				// Lowest priority: only runs when the main thread is waiting (frame cap, V-blank)
				gWriterThread = threadCreate(writerThreadFunc, nullptr, 0x8000, 0x3f, -2, true);
			}

			void writeAreaSummary(bool synchronousReport = false)
			{
				addVisitToAggregate();
				const double seconds = (double)gArea.mFrameTicks / (double)SYSCLOCK_ARM11;
				if (gArea.mFrames > 0 && seconds >= 1.0)		// Skip very short visits, like transitions
				{
					const double frames = (double)gArea.mFrames;
					const auto avg = [&](Section section)  { return (double)gArea.mSection[section] / TICKS_PER_MS / frames; };
					const double stepsPerSecond = (double)gArea.mSimSteps / seconds;
					char buffer[640];
					snprintf(buffer, sizeof(buffer),
							 "PROFILER AREA SUMMARY %s | %.1f s | shown_fps %.1f | missed_refreshes %u | game_speed %.1f%% | frame avg %.2f max %.2f ms"
							 " | game logic %.2f | render %.2f (planes %.2f, sprites %.2f, drawer %.2f) | present %.2f (gpu wait %.2f, swap wait %.2f) | frame cap wait %.2f",
							 gAreaLong, seconds, frames / seconds, (unsigned)gArea.mMissedRefreshes,
							 (gAreaTargetStepRate > 0.0f) ? stepsPerSecond * 100.0 / (double)gAreaTargetStepRate : 0.0,
							 (double)gArea.mFrameTicks / TICKS_PER_MS / frames, (double)gArea.mFrameMax / TICKS_PER_MS,
							 avg(SIMULATION), avg(RENDER), avg(SW_PLANES), avg(SW_SPRITES), avg(DRAWER),
							 avg(PRESENT_SDL), avg(PRESENT_GPU_WAIT), avg(PRESENT_SWAP_WAIT), avg(WAIT));
					logLine(buffer);
				}
				gArea = AreaTotals();
				writeReport(synchronousReport);
			}

			void updateArea()
			{
				if (!EmulatorInterface::hasInstance())
					return;

				EmulatorInterface& emulatorInterface = EmulatorInterface::instance();
				const uint8 gameMode = emulatorInterface.readMemory8(0xfffff600);
				const uint16 zoneAct = emulatorInterface.readMemory16(0xfffffe10);
				const uint32_t key = ((uint32_t)gameMode << 16) | zoneAct;
				if (key == gLastAreaKey)
				{
					gCandidateAreaKey = key;
					return;
				}

				// Values that flicker (e.g. in menus, where a background scene runs) or only last for a moment are no area change
				if (key != gCandidateAreaKey)
				{
					gCandidateAreaKey = key;
					gCandidateAreaFrames = 0;
				}
				if (++gCandidateAreaFrames < AREA_CHANGE_FRAMES && gLastAreaKey != 0xffffffff)
					return;
				gLastAreaKey = key;
				writeAreaSummary();		// For the area that was just left

				switch (gameMode)
				{
					case 0x00:  snprintf(gAreaShort, sizeof(gAreaShort), "Menu");  snprintf(gAreaLong, sizeof(gAreaLong), "SEGA screen / A.I.R. menus");  break;
					case 0x04:  snprintf(gAreaShort, sizeof(gAreaShort), "Title"); snprintf(gAreaLong, sizeof(gAreaLong), "Title screen / menu background");  break;
					case 0x08:  snprintf(gAreaShort, sizeof(gAreaShort), "Demo");  snprintf(gAreaLong, sizeof(gAreaLong), "Demo");  break;
					case 0x2c:  snprintf(gAreaShort, sizeof(gAreaShort), "BSmenu"); snprintf(gAreaLong, sizeof(gAreaLong), "Blue Sphere menu");  break;
					case 0x30:  snprintf(gAreaShort, sizeof(gAreaShort), "BSres"); snprintf(gAreaLong, sizeof(gAreaLong), "Blue Sphere results");  break;
					case 0x34:  snprintf(gAreaShort, sizeof(gAreaShort), "SS");    snprintf(gAreaLong, sizeof(gAreaLong), "Special stage");  break;
					case 0x48:  snprintf(gAreaShort, sizeof(gAreaShort), "SSres"); snprintf(gAreaLong, sizeof(gAreaLong), "After special stage (mode 0x48)");  break;
					case 0x88:  snprintf(gAreaShort, sizeof(gAreaShort), "Load");  snprintf(gAreaLong, sizeof(gAreaLong), "Demo start (mode 0x88)");  break;
					case 0x8c:  snprintf(gAreaShort, sizeof(gAreaShort), "Card");  snprintf(gAreaLong, sizeof(gAreaLong), "Level start / title card");  break;
					case 0x0c:
					{
						// In a level: zone and act
						const uint8 zone = (uint8)(zoneAct >> 8);
						const uint8 act = (uint8)(zoneAct & 0xff) + 1;
						if (zone < sizeof(ZONE_NAMES) / sizeof(ZONE_NAMES[0]))
						{
							const bool isBonusStage = (zone >= 0x13);
							snprintf(gAreaShort, sizeof(gAreaShort), isBonusStage ? "%s" : "%s%u", ZONE_NAMES[zone].mShort, (unsigned)act);
							snprintf(gAreaLong, sizeof(gAreaLong), isBonusStage ? "%s" : "%s Act %u", ZONE_NAMES[zone].mLong, (unsigned)act);
						}
						else
						{
							snprintf(gAreaShort, sizeof(gAreaShort), "Z%02X-%u", zone, (unsigned)act);
							snprintf(gAreaLong, sizeof(gAreaLong), "Zone 0x%02x Act %u", zone, (unsigned)act);
						}
						break;
					}
					default:
						snprintf(gAreaShort, sizeof(gAreaShort), "M%02X", gameMode);
						snprintf(gAreaLong, sizeof(gAreaLong), "Game mode 0x%02x", gameMode);
						break;
				}
				logLine(std::string("PROFILER area -> ") + gAreaLong + " (game mode " + rmx::hexString(gameMode, 2) + ", zone/act " + rmx::hexString(zoneAct, 4) + ")");
			}

			void getLineValues(const Line& line, double& outAverage, double& outMax)
			{
				const double frames = (gWindowFrames > 0) ? (double)gWindowFrames : 1.0;
				uint64_t total;
				uint64_t maximum;
				if (line.mSection == -1)
				{
					total = gWindowFrameTotal;
					maximum = gWindowFrameMax;
				}
				else if (line.mSection == -2)
				{
					total = gWindowOtherTotal;
					maximum = gWindowOtherMax;
				}
				else
				{
					total = gWindowTotal[line.mSection];
					maximum = gWindowMax[line.mSection];
				}
				outAverage = toMs(total) / frames;
				outMax = toMs(maximum);
			}

			void calibrate()
			{
				// Measure what a begin/end pair costs (two system tick reads), to be able to tell the profiler's overhead.
				// The loop mimics "beginSection" / "endSection" on a scratch array.
				const int NUM_CALLS = 1024;
				static volatile uint64_t scratch[2] = { 0 };
				const uint64_t start = svcGetSystemTick();
				for (int i = 0; i < NUM_CALLS; ++i)
				{
					scratch[0] = svcGetSystemTick();
					scratch[1] += svcGetSystemTick() - scratch[0];
				}
				gTicksPerSectionCall = (double)(svcGetSystemTick() - start) / (double)NUM_CALLS;
			}

			void prepareReport(uint64_t now)
			{
				const double windowMs = toMs(now - gWindowStart);
				const double frames = (gWindowFrames > 0) ? (double)gWindowFrames : 1.0;
				const double shownFps = (windowMs > 0.0) ? (double)gWindowFrames * 1000.0 / windowMs : 0.0;
				const double stepsPerSecond = (windowMs > 0.0) ? (double)gWindowSimSteps * 1000.0 / windowMs : 0.0;
				const double gameSpeedPercent = (gTargetStepRate > 0.0f) ? stepsPerSecond * 100.0 / (double)gTargetStepRate : 0.0;
				const double stepsPerFrame = (double)gWindowSimSteps / frames;
				const uint32_t workerTicks = gWorkerTicks.exchange(0);
				const double workerMsPerFrame = toMs(workerTicks) / frames;
				const double workerCorePercent = (windowMs > 0.0) ? toMs(workerTicks) * 100.0 / windowMs : 0.0;
				const double sectionCallsPerFrame = (double)gWindowSectionCalls / frames;
				const double timingOverheadMs = toMs((uint64_t)(sectionCallsPerFrame * gTicksPerSectionCall));
				const double reportMs = toMs(gWindowTotal[PROFILER]) / frames;

				// Bottom screen lines
				{
					int index = 0;
					const auto addLine = [&](const char* format, auto... args)
					{
						if (index < SCREEN_LINES)
						{
							char text[64];
							snprintf(text, sizeof(text), format, args...);
							snprintf(gScreenLines[index], SCREEN_LINE_LENGTH + 1, "%-39.39s", text);
							++index;
						}
					};

					char areaAndBuild[32];
					snprintf(areaAndBuild, sizeof(areaAndBuild), "%s %s", gAreaShort, S3AIR_3DS_BUILD_ID);
					addLine("%-20.20s %8s %7s", areaAndBuild, "avg ms", "max");
					addLine("Shown FPS %5.1f   missed refreshes %u", shownFps, (unsigned)gWindowMissedRefreshes);
					if (gWindowSimSteps > 0)
						addLine("Game speed %5.1f%%  %5.1f steps/s, max %u", gameSpeedPercent, stepsPerSecond, (unsigned)gWindowSimStepsMax);
					else
						addLine("Game speed: game logic not running");
					for (const Line& line : LINES)
					{
						if (!line.mOnScreen)
							continue;
						double average, maximum;
						getLineValues(line, average, maximum);
						addLine("%-20s %8.2f %7.2f", line.mLabel, average, maximum);
					}
					addLine("Audio wkr %5.2fms %4.1f%%  Prof %4.2fms", workerMsPerFrame, workerCorePercent, timingOverheadMs + reportMs);
					gNextScreenLine = 0;
				}

				// Log file: one compact line per report, "average/max" in milliseconds
				{
					char buffer[1280];
					int length = snprintf(buffer, sizeof(buffer), "PROFILER area=%s build=%s shown_fps=%.1f missed_refreshes=%u game_speed=%.1f%% steps_per_s=%.1f steps_per_frame=%.2f/%u",
										  gAreaLong, S3AIR_3DS_BUILD_ID, shownFps, (unsigned)gWindowMissedRefreshes, gameSpeedPercent, stepsPerSecond, stepsPerFrame, (unsigned)gWindowSimStepsMax);
					for (const Line& line : LINES)
					{
						double average, maximum;
						getLineValues(line, average, maximum);
						const char* label = line.mLabel;
						while (*label == ' ')
							++label;
						if (length > 0 && length < (int)sizeof(buffer))
							length += snprintf(buffer + length, sizeof(buffer) - length, " | %s %.2f/%.2f", label, average, maximum);
					}
					if (length > 0 && length < (int)sizeof(buffer))
						length += snprintf(buffer + length, sizeof(buffer) - length, " | Audio workers %.2f ms/frame %.1f%%", workerMsPerFrame, workerCorePercent);
					if (length > 0 && length < (int)sizeof(buffer))
						length += snprintf(buffer + length, sizeof(buffer) - length, " | Profiler timing %.3f ms/frame (%.0f sections), reports %.3f ms/frame", timingOverheadMs, sectionCallsPerFrame, reportMs);
					if (length > 0 && length < (int)sizeof(buffer))
						snprintf(buffer + length, sizeof(buffer) - length, " | Sprites per frame: VDP %.0f, palette %.0f, component %.0f",
								 (double)gWindowCounts[COUNT_SPR_VDP] / frames, (double)gWindowCounts[COUNT_SPR_PALETTE] / frames, (double)gWindowCounts[COUNT_SPR_COMPONENT] / frames);
					logLine(buffer);
				}
			}

			void initConsole()
			{
				if (!gConsoleInitialized)
				{
					consoleInit(GFX_BOTTOM, &gConsole);
					consoleClear();
					gConsoleInitialized = true;
				}
				consoleSelect(&gConsole);
			}

			void presentConsole()
			{
				u16 width = 0;
				u16 height = 0;
				u8* framebuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &width, &height);
				if (nullptr != framebuffer)
				{
					GSPGPU_FlushDataCache(framebuffer, (u32)width * (u32)height * 2);
					gfxScreenSwapBuffers(GFX_BOTTOM, false);
				}
			}

			void printPendingScreenLines()
			{
				if (gNextScreenLine >= SCREEN_LINES)
					return;
				if (!aptIsActive())		// The bottom screen belongs to the system while suspended
					return;

				initConsole();

				// Overwrite the previous table in place instead of clearing the screen first, so it doesn't flicker
				const int end = std::min(gNextScreenLine + SCREEN_LINES_PER_FRAME, SCREEN_LINES);
				for (; gNextScreenLine < end; ++gNextScreenLine)
				{
					gConsole.cursorX = 1;	// libctru console coordinates are 1-based
					gConsole.cursorY = gNextScreenLine + 1;
					fputs(gScreenLines[gNextScreenLine], stdout);		// No newline: the last line would scroll the console
				}

				if (gNextScreenLine >= SCREEN_LINES)
				{
					presentConsole();
				}
			}

			void resetWindow(uint64_t now)
			{
				for (int i = 0; i < _NUM; ++i)
				{
					gWindowTotal[i] = 0;
					gWindowMax[i] = 0;
				}
				gWindowFrameTotal = 0;
				gWindowFrameMax = 0;
				gWindowOtherTotal = 0;
				gWindowOtherMax = 0;
				gWindowFrames = 0;
				gWindowSimSteps = 0;
				gWindowSimStepsMax = 0;
				gWindowMissedRefreshes = 0;
				gWindowSectionCalls = 0;
				for (int i = 0; i < _NUM_COUNTERS; ++i)
					gWindowCounts[i] = 0;
				gWindowStart = now;
			}
		}

		void finish()
		{
			writeAreaSummary(true);		// On exit: write the report directly
			flushLogLinesNow();
		}

		void showStartupInfo()
		{
			// Before the game data and scripts get loaded: if anything fails there, at least the build is visible
			initConsole();
			gConsole.cursorX = 1;
			gConsole.cursorY = 1;
			printf("Sonic 3 A.I.R. for New 3DS\n");
			printf("Unofficial port by heccfluffin\n");
			printf("Original game: Eukaryot & contributors\n\n");
			printf("Build %s\n\n", S3AIR_3DS_BUILD_ID);
			printf("Loading...\n");
			presentConsole();
		}

		uint64_t getTicks()
		{
			return svcGetSystemTick();
		}

		void beginSection(Section section)
		{
			++gFrameSectionCalls;
			gSectionStart[section] = svcGetSystemTick();
		}

		void endSection(Section section)
		{
			gFrameAccum[section] += svcGetSystemTick() - gSectionStart[section];
		}

		void addCount(Counter counter)
		{
			++gFrameCounts[counter];
		}

		void addSimulationStep()
		{
			++gFrameSimSteps;
		}

		void setTargetStepRate(float stepsPerSecond)
		{
			gTargetStepRate = stepsPerSecond;
		}

		void addWorkerTicks(uint32_t ticks)
		{
			gWorkerTicks.fetch_add(ticks, std::memory_order_relaxed);
		}

		void endFrame()
		{
			const uint64_t now = svcGetSystemTick();
			if (gLastFrameEnd == 0)
			{
				// Very first frame: just start measuring from here
				calibrate();
				startWriterThread();
				const uint64_t afterCalibration = svcGetSystemTick();
				gLastFrameEnd = afterCalibration;
				resetWindow(afterCalibration);
				for (int i = 0; i < _NUM; ++i)
					gFrameAccum[i] = 0;
				gFrameSimSteps = 0;
				gFrameSectionCalls = 0;
				gWorkerTicks.store(0);
				return;
			}

			// Frame time: from the end of the previous frame's presenting to the end of this one's, so this includes everything
			const uint64_t frameTicks = now - gLastFrameEnd;
			gLastFrameEnd = now;

			uint64_t topLevelTicks = 0;
			for (Section section : TOP_LEVEL_SECTIONS)
				topLevelTicks += gFrameAccum[section];
			const uint64_t otherTicks = (frameTicks > topLevelTicks) ? (frameTicks - topLevelTicks) : 0;

			for (int i = 0; i < _NUM; ++i)
			{
				gArea.mSection[i] += gFrameAccum[i];
				gWindowTotal[i] += gFrameAccum[i];
				if (gFrameAccum[i] > gWindowMax[i])
					gWindowMax[i] = gFrameAccum[i];
				gFrameAccum[i] = 0;
			}
			gWindowFrameTotal += frameTicks;
			gArea.mFrameTicks += frameTicks;
			gArea.mFrameMax = std::max(gArea.mFrameMax, frameTicks);
			++gArea.mFrames;
			gArea.mSimSteps += gFrameSimSteps;
			for (int i = 0; i < _NUM_COUNTERS; ++i)
			{
				gArea.mCounts[i] += gFrameCounts[i];
				gWindowCounts[i] += gFrameCounts[i];
				gFrameCounts[i] = 0;
			}
			gAreaTargetStepRate = gTargetStepRate;
			if (frameTicks > gWindowFrameMax)
				gWindowFrameMax = frameTicks;
			gWindowOtherTotal += otherTicks;
			if (otherTicks > gWindowOtherMax)
				gWindowOtherMax = otherTicks;
			gWindowSimSteps += gFrameSimSteps;
			if (gFrameSimSteps > gWindowSimStepsMax)
				gWindowSimStepsMax = gFrameSimSteps;
			gFrameSimSteps = 0;
			gWindowSectionCalls += gFrameSectionCalls;
			gFrameSectionCalls = 0;
			++gWindowFrames;

			// A frame that took about N screen refreshes was on screen for N refreshes, i.e. N - 1 refreshes didn't show a new frame
			const uint64_t refreshes = (frameTicks + REFRESH_TICKS / 2) / REFRESH_TICKS;
			if (refreshes > 1)
			{
				gWindowMissedRefreshes += (uint32_t)(refreshes - 1);
				gArea.mMissedRefreshes += (uint32_t)(refreshes - 1);
			}

			// The profiler's own work: counted as part of the next frame, in its own section, so it's not hidden in "Unaccounted"
			beginSection(PROFILER);
			updateArea();
			if (now - gWindowStart >= REPORT_INTERVAL_TICKS)
			{
				prepareReport(now);
				resetWindow(now);
			}
			printPendingScreenLines();
			endSection(PROFILER);
		}
	}
}

#endif
