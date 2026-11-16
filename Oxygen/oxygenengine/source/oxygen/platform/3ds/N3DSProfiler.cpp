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


namespace n3ds
{
	namespace profiler
	{
		namespace
		{
			const constexpr double TICKS_PER_MS = (double)SYSCLOCK_ARM11 / 1000.0;
			const constexpr uint64_t REPORT_INTERVAL_TICKS = (uint64_t)SYSCLOCK_ARM11 * 2;		// Every 2 seconds

			// Current frame
			uint64_t gSectionStart[_NUM] = { 0 };
			uint64_t gFrameAccum[_NUM] = { 0 };
			uint32_t gFrameSimSteps = 0;

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
			uint64_t gWindowStart = 0;
			uint64_t gLastFrameEnd = 0;

			std::atomic<uint32_t> gWorkerTicks { 0 };

			PrintConsole gConsole;
			bool gConsoleInitialized = false;

			// Top-level sections, i.e. the ones that don't overlap each other
			const Section TOP_LEVEL_SECTIONS[] = { SIMULATION, UI_UPDATE, AUDIO_MAIN, RENDER, PRESENT_REV, PRESENT_SDL, WAIT };

			struct Line
			{
				const char* mLabel;
				int mSection;	// -1 for "Frame total", -2 for "Other"
			};
			const Line LINES[] =
			{
				{ "Frame total",       -1 },
				{ " Game logic",       SIMULATION },
				{ "   Scripts",        SCRIPTS },
				{ " Menus/UI update",  UI_UPDATE },
				{ " Audio (main)",     AUDIO_MAIN },
				{ " Render",           RENDER },
				{ "   VDP refresh",    REFRESH },
				{ "   Geometry",       GEOMETRY },
				{ "   SW game screen", SW_RENDER },
				{ "     Planes",       SW_PLANES },
				{ "     Sprites",      SW_SPRITES },
				{ "     Blur",         SW_BLUR },
				{ "   Drawer/UI",      DRAWER },
				{ " Present: bswap",   PRESENT_REV },
				{ " Present: SDL",     PRESENT_SDL },
				{ " Frame cap wait",   WAIT },
				{ " Other",            -2 },
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

			void report(uint64_t now)
			{
				const double windowMs = toMs(now - gWindowStart);
				const double fps = (windowMs > 0.0) ? (double)gWindowFrames * 1000.0 / windowMs : 0.0;
				const double stepsPerFrame = (gWindowFrames > 0) ? (double)gWindowSimSteps / (double)gWindowFrames : 0.0;
				const uint32_t workerTicks = gWorkerTicks.exchange(0);
				const double workerMsPerFrame = (gWindowFrames > 0) ? toMs(workerTicks) / (double)gWindowFrames : 0.0;
				const double workerCorePercent = (windowMs > 0.0) ? toMs(workerTicks) * 100.0 / windowMs : 0.0;

				// Bottom screen
				{
					if (!gConsoleInitialized)
					{
						consoleInit(GFX_BOTTOM, &gConsole);
						gConsoleInitialized = true;
					}
					consoleSelect(&gConsole);
					consoleClear();
					printf("S3AIR 3DS profiler   ms/frame avg  max\n");
					printf("FPS %5.1f   game steps/frame %.2f (max %u)\n", fps, stepsPerFrame, (unsigned)gWindowSimStepsMax);
					for (const Line& line : LINES)
					{
						double average, maximum;
						getLineValues(line, average, maximum);
						printf("%-20s %8.2f %7.2f\n", line.mLabel, average, maximum);
					}
					printf("Audio workers %6.2f ms/frame %5.1f%%\n", workerMsPerFrame, workerCorePercent);

					u16 width = 0;
					u16 height = 0;
					u8* framebuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &width, &height);
					if (nullptr != framebuffer)
					{
						GSPGPU_FlushDataCache(framebuffer, (u32)width * (u32)height * 2);
						gfxScreenSwapBuffers(GFX_BOTTOM, false);
					}
				}

				// Log file: one compact line per report, "average/max" in milliseconds
				{
					char buffer[1024];
					int length = snprintf(buffer, sizeof(buffer), "PROFILER fps=%.1f steps=%.2f/%u", fps, stepsPerFrame, (unsigned)gWindowSimStepsMax);
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
						snprintf(buffer + length, sizeof(buffer) - length, " | Audio workers %.2f ms/frame %.1f%%", workerMsPerFrame, workerCorePercent);
					RMX_LOG_INFO(buffer);
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
				gWindowStart = now;
			}
		}

		uint64_t getTicks()
		{
			return svcGetSystemTick();
		}

		void beginSection(Section section)
		{
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
				gLastFrameEnd = now;
				resetWindow(now);
				for (int i = 0; i < _NUM; ++i)
					gFrameAccum[i] = 0;
				gFrameSimSteps = 0;
				gWorkerTicks.store(0);
				return;
			}

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
			++gWindowFrames;

			if (now - gWindowStart >= REPORT_INTERVAL_TICKS)
			{
				report(now);

				// Don't count the time spent on reporting towards the next frame
				const uint64_t afterReport = svcGetSystemTick();
				gLastFrameEnd = afterReport;
				resetWindow(afterReport);
			}
		}
	}
}

#endif
