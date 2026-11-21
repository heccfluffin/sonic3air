// Host test: stand-ins for the parts of libctru (the 3DS system library) that the 3DS-specific code uses.
// Clock, battery, region, touch screen and SD card speed are controlled by the test through "fake::" (see fake3ds.h).
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <mutex>
#include <condition_variable>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int32_t Result;
typedef u32 Handle;

#define R_SUCCEEDED(res) ((res) >= 0)
#define R_FAILED(res) ((res) < 0)
#define SYSCLOCK_ARM11 268111856
#define CUR_THREAD_HANDLE 0xFFFF8000

// Ticks and threads
u64 svcGetSystemTick();
void svcSleepThread(int64_t ns);
Result svcGetThreadPriority(s32* out, Handle handle);

struct ThreadInfo;
typedef ThreadInfo* Thread;
typedef void (*ThreadFunc)(void*);
Thread threadCreate(ThreadFunc entrypoint, void* arg, size_t stackSize, int prio, int core, bool detached);

struct LightLock { std::mutex mMutex; };
void LightLock_Init(LightLock* lock);
void LightLock_Lock(LightLock* lock);
void LightLock_Unlock(LightLock* lock);

enum ResetType { RESET_ONESHOT = 0, RESET_STICKY = 1, RESET_PULSE = 2 };
struct LightEvent { std::mutex mMutex; std::condition_variable mCondition; bool mSignaled = false; };
void LightEvent_Init(LightEvent* event, ResetType resetType);
void LightEvent_Wait(LightEvent* event);
void LightEvent_Signal(LightEvent* event);

// Screen
enum gfxScreen_t { GFX_TOP = 0, GFX_BOTTOM = 1 };
enum gfx3dSide_t { GFX_LEFT = 0, GFX_RIGHT = 1 };
enum GSPGPU_FramebufferFormat { GSP_RGBA8_OES = 0, GSP_BGR8_OES = 1, GSP_RGB565_OES = 2, GSP_RGB5_A1_OES = 3, GSP_RGBA4_OES = 4 };
u8* gfxGetFramebuffer(gfxScreen_t screen, gfx3dSide_t side, u16* width, u16* height);
GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t screen);
void gfxSetScreenFormat(gfxScreen_t screen, GSPGPU_FramebufferFormat format);
void gfxSetDoubleBuffering(gfxScreen_t screen, bool enable);
void gfxScreenSwapBuffers(gfxScreen_t screen, bool hasStereo);
Result GSPGPU_FlushDataCache(const void* address, u32 size);

// Touch screen
#define KEY_TOUCH (1u << 20)
struct touchPosition { u16 px; u16 py; };
void hidScanInput();
u32 hidKeysDown();
u32 hidKeysHeld();
u32 hidKeysUp();
void hidTouchRead(touchPosition* pos);

// Applet manager
enum APT_HookType { APTHOOK_ONSUSPEND = 0, APTHOOK_ONRESTORE, APTHOOK_ONSLEEP, APTHOOK_ONWAKEUP, APTHOOK_ONEXIT };
typedef void (*aptHookFn)(APT_HookType hook, void* param);
struct aptHookCookie { aptHookFn callback; void* param; };
void aptHook(aptHookCookie* cookie, aptHookFn callback, void* param);
bool aptIsActive();
bool aptShouldClose();

// System settings and power
enum CFG_Region { CFG_REGION_JPN = 0, CFG_REGION_USA = 1, CFG_REGION_EUR = 2, CFG_REGION_AUS = 3 };
Result cfguInit();
void cfguExit();
Result CFGU_SecureInfoGetRegion(u8* region);
Result CFGU_GetConfigInfoBlk2(u32 size, u32 blkID, void* outData);
Result ptmuInit();
Result PTMU_GetBatteryLevel(u8* out);
Result PTMU_GetBatteryChargeState(u8* out);
