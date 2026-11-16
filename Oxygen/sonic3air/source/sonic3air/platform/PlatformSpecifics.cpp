/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"
#include "sonic3air/platform/PlatformSpecifics.h"

#if defined(PLATFORM_VITA)
	#include <vitasdk.h>
	#include <vitaGL.h>
	#include "sonic3air/platform/vita/trophies.h"
#endif

#if defined(PLATFORM_3DS)
	#include <3ds.h>
	#include <sys/stat.h>
	#include <unistd.h>

	extern "C"
	{
		extern char* fake_heap_start;
		extern char* fake_heap_end;
		extern u32 __ctru_heap;
		extern u32 __ctru_heap_size;
		extern u32 __ctru_linear_heap;
		extern u32 __ctru_linear_heap_size;

		// Replaces libctru's default heap setup, which gives everything except 32 MB of linear memory to the application heap.
		// In the New 3DS 178 MB memory mode, that is more than the 96 MB heap address range, so the allocation fails and the app panics on startup.
		// Here, the application heap (used by malloc / new) is capped to the heap address range, and the rest goes to the linear heap.
		void __system_allocateHeaps(void)
		{
			Handle reslimit = 0;
			if (R_FAILED(svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE)))
				svcBreak(USERBREAK_PANIC);

			s64 maxCommit = 0;
			s64 currentCommit = 0;
			ResourceLimitType reslimitType = RESLIMIT_COMMIT;
			svcGetResourceLimitLimitValues(&maxCommit, reslimit, &reslimitType, 1);
			svcGetResourceLimitCurrentValues(&currentCommit, reslimit, &reslimitType, 1);
			svcCloseHandle(reslimit);

			const u32 remaining = (u32)(maxCommit - currentCommit) & ~0xFFFu;
			const u32 maxHeapSize = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN;
			const u32 minLinearHeapSize = 16u << 20;	// Framebuffers, audio buffers

			u32 heapSize = (remaining > 2 * minLinearHeapSize) ? (remaining - minLinearHeapSize) : (remaining / 2);
			if (heapSize > maxHeapSize)
				heapSize = maxHeapSize;
			heapSize &= ~0xFFFu;

			__ctru_heap_size = heapSize;
			__ctru_linear_heap_size = remaining - heapSize;

			if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size, MEMOP_ALLOC, (MemPerm)(MEMPERM_READ | MEMPERM_WRITE))))
				svcBreak(USERBREAK_PANIC);
			if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR, (MemPerm)(MEMPERM_READ | MEMPERM_WRITE))))
				svcBreak(USERBREAK_PANIC);

			mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);

			fake_heap_start = (char*)__ctru_heap;
			fake_heap_end = fake_heap_start + __ctru_heap_size;
		}
	}
#endif


namespace
{
#if defined(PLATFORM_VITA)
	int init_msg_dialog(const char* msg)
	{
		SceMsgDialogUserMessageParam msg_param;
		memset(&msg_param, 0, sizeof(msg_param));
		msg_param.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
		msg_param.msg = (SceChar8*)msg;

		SceMsgDialogParam param;
		sceMsgDialogParamInit(&param);
		_sceCommonDialogSetMagicNumber(&param.commonParam);
		param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
		param.userMsgParam = &msg_param;

		return sceMsgDialogInit(&param);
	}

	void warning(const char* msg)
	{
		init_msg_dialog(msg);

		while (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED)
		{
			vglSwapBuffers(GL_TRUE);
		}
		sceMsgDialogTerm();
	}

	void fatal_error(const char* msg)
	{
		vglInit(0);
		warning(msg);
		sceKernelExitProcess(0);
		while (1);
	}

	int file_exists(const char* path)
	{
		SceIoStat stat;
		return sceIoGetstat(path, &stat) >= 0;
	}
#endif
}


void PlatformSpecifics::platformStartup()
{
#if defined(PLATFORM_VITA)
	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);

	// Check for libshacccg.suprx existence
	if (!file_exists("ur0:/data/libshacccg.suprx") && !file_exists("ur0:/data/external/libshacccg.suprx"))
		fatal_error("Error: libshacccg.suprx is not installed.");

	vglInitExtended(0, 960, 544, 12 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);

	// Initing trophy system
	SceIoStat st;
	int r = trophies_init();
	if (r < 0 && sceIoGetstat("ux0:data/sonic3air/trophies.chk", &st) < 0)
	{
		FILE* f = fopen("ux0:data/sonic3air/trophies.chk", "w");
		fclose(f);
		warning("This game features unlockable trophies but NoTrpDrm is not installed. If you want to be able to unlock trophies, please install it.");
	}

	changeWorkingDirectory(L"ux0:/data/sonic3air");
#endif

#if defined(PLATFORM_3DS)
	// New 3DS / New 2DS XL: switch to 804 MHz and enable the L2 cache (no effect on Old 3DS models)
	osSetSpeedupEnable(true);

	// SDL ignores joystick button presses while no window has keyboard focus, which on 3DS can happen e.g. after switching to fullscreen,
	// leaving the built-in buttons without any effect. The game is always the foreground app on 3DS, so accept joystick input regardless.
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

	// Allow up to 30% of the system core to be used by helper threads (e.g. audio)
	APT_SetAppCpuTimeLimit(30);

	// All game files live in this directory on the SD card
	mkdir("sdmc:/3ds", 0777);
	mkdir("sdmc:/3ds/sonic3air", 0777);
	mkdir("sdmc:/3ds/sonic3air/savedata", 0777);
	mkdir("sdmc:/3ds/sonic3air/savedata/mods", 0777);
	chdir("sdmc:/3ds/sonic3air");
#endif
}
