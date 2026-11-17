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

#include <3ds.h>
#include <atomic>
#include <stdio.h>

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

					addLine("%-20.20s %8s %7s", "Build " S3AIR_3DS_BUILD_ID, "avg ms", "max");
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
					int length = snprintf(buffer, sizeof(buffer), "PROFILER build=%s shown_fps=%.1f missed_refreshes=%u game_speed=%.1f%% steps_per_s=%.1f steps_per_frame=%.2f/%u",
										  S3AIR_3DS_BUILD_ID, shownFps, (unsigned)gWindowMissedRefreshes, gameSpeedPercent, stepsPerSecond, stepsPerFrame, (unsigned)gWindowSimStepsMax);
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
						snprintf(buffer + length, sizeof(buffer) - length, " | Profiler timing %.3f ms/frame (%.0f sections), reports %.3f ms/frame", timingOverheadMs, sectionCallsPerFrame, reportMs);
					RMX_LOG_INFO(buffer);
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
				gWindowStart = now;
			}
		}

		void showStartupInfo()
		{
			// Before the game data and scripts get loaded: if anything fails there, at least the build is visible
			initConsole();
			gConsole.cursorX = 1;
			gConsole.cursorY = 1;
			printf("Sonic 3 A.I.R. for New 3DS (unofficial)\n\n");
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
				gWindowTotal[i] += gFrameAccum[i];
				if (gFrameAccum[i] > gWindowMax[i])
					gWindowMax[i] = gFrameAccum[i];
				gFrameAccum[i] = 0;
			}
			gWindowFrameTotal += frameTicks;
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
				gWindowMissedRefreshes += (uint32_t)(refreshes - 1);

			// The profiler's own work: counted as part of the next frame, in its own section, so it's not hidden in "Unaccounted"
			beginSection(PROFILER);
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
