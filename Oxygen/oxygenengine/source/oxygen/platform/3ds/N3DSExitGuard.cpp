/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: making sure that exiting the game always returns to the HOME Menu.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSExitGuard.h"

#include <3ds.h>
#include <stdio.h>


namespace n3ds
{
	namespace exitguard
	{
		namespace
		{
			volatile const char* gCurrentStep = "exit started";
			Thread gWatchdogThread = nullptr;
			unsigned int gTimeoutSeconds = 4;

			void watchdogFunc(void*)
			{
				svcSleepThread((s64)gTimeoutSeconds * 1000000000LL);

				// Still running: a shutdown step hangs. Note which one (with plain C file functions, independent of the engine),
				// then end the process.
				FILE* file = fopen("sdmc:/3ds/sonic3air/savedata/exit_hang.txt", "w");
				if (nullptr != file)
				{
					fprintf(file, "Exiting took longer than %u seconds, ended by the exit watchdog.\nHanging step: %s\n", gTimeoutSeconds, (const char*)gCurrentStep);
					fclose(file);
				}
				// Close the application properly towards the system first (like libctru's normal exit does), otherwise the
				// system shows "An error has occurred, forcing the software to close" instead of just returning to the HOME Menu
				aptExit();
				svcExitProcess();
			}
		}

		void start(unsigned int seconds)
		{
			if (nullptr != gWatchdogThread)
				return;

			gTimeoutSeconds = seconds;
			// Higher priority (lower value) than the main thread, so it gets to run even if the main thread is busy
			s32 mainPriority = 0x30;
			svcGetThreadPriority(&mainPriority, CUR_THREAD_HANDLE);
			gWatchdogThread = threadCreate(watchdogFunc, nullptr, 0x4000, std::max(0x18, (int)mainPriority - 1), -2, true);
			RMX_LOG_INFO("Exit: watchdog started (" << seconds << " s)");
		}

		void showExitMessage()
		{
			// When the app gets closed from the HOME Menu, the screens belong to the system
			if (!aptIsActive() || aptShouldClose())
				return;

			static PrintConsole console;
			consoleInit(GFX_BOTTOM, &console);	// Clears the bottom screen (and replaces the profiler output)
			consoleSelect(&console);
			console.cursorX = 1;
			console.cursorY = 13;
			printf("          Sonic 3 A.I.R.\n\n");
			printf("     Saving settings, please wait...");

			u16 width = 0;
			u16 height = 0;
			u8* framebuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &width, &height);
			if (nullptr != framebuffer)
			{
				GSPGPU_FlushDataCache(framebuffer, (u32)width * (u32)height * 2);
				gfxScreenSwapBuffers(GFX_BOTTOM, false);
			}
		}

		void step(const char* name)
		{
			gCurrentStep = name;
			RMX_LOG_INFO("Exit: " << name);
		}
	}
}

#endif
