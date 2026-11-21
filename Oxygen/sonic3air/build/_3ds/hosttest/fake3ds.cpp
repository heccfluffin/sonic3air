// Host test: the fake 3DS behind fake/3ds.h
#include "fake/3ds.h"
#include "fake/oxygen/pch.h"
#include "fake3ds.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
	const auto gStart = std::chrono::steady_clock::now();
	double gExtraSeconds = 0.0;			// From "advanceSeconds"
	time_t gClockBase = 0;				// The fake 3DS clock at "gStart"
	int gBatteryLevel = 4;
	bool gCharging = false;
	int gRegion = CFG_REGION_EUR;
	std::u16string gNickname = u"Tester";
	int gBirthdayMonth = 0;
	int gBirthdayDay = 0;
	bool gTouching = false, gWasTouching = false, gNextTouching = false;
	int gTouchX = 0, gTouchY = 0;
	int gSDCardDelayMs = 0;
	u16 gFramebuffer[240 * 320];
	GSPGPU_FramebufferFormat gFormat = GSP_BGR8_OES;
	int gSwapCount = 0;
	uint32_t gTopTint = 0xffffff;
	std::vector<std::string> gLog;
	std::mutex gLogMutex;

	double secondsSinceStart()
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - gStart).count() + gExtraSeconds;
	}
}

namespace fake
{
	void setTime(int year, int month, int day, int hour, int minute)
	{
		struct tm t = {};
		t.tm_year = year - 1900;
		t.tm_mon = month - 1;
		t.tm_mday = day;
		t.tm_hour = hour;
		t.tm_min = minute;
		gClockBase = timegm(&t) - (time_t)secondsSinceStart();
	}
	void advanceSeconds(double seconds)	{ gExtraSeconds += seconds; }
	void setBattery(int level, bool charging)	{ gBatteryLevel = level; gCharging = charging; }
	void setRegion(int region)			{ gRegion = region; }
	void setNickname(const std::u16string& name)	{ gNickname = name; }
	void setBirthday(int month, int day)	{ gBirthdayMonth = month; gBirthdayDay = day; }
	void setTouch(bool touching, int x, int y)	{ gNextTouching = touching; if (touching) { gTouchX = x; gTouchY = y; } }
	void setSDCardDelay(int ms)			{ gSDCardDelayMs = ms; }
	int getSwapCount()					{ return gSwapCount; }
	uint32_t getTopScreenTint()			{ return gTopTint; }

	bool saveScreenshot(const std::string& filename)
	{
		std::ofstream file(filename, std::ios::binary);
		file << "P6\n320 240\n255\n";
		for (int y = 0; y < 240; ++y)
		{
			for (int x = 0; x < 320; ++x)
			{
				const u16 pixel = gFramebuffer[x * 240 + (239 - y)];
				const unsigned char rgb[3] = { (unsigned char)(((pixel >> 11) & 0x1f) * 255 / 31), (unsigned char)(((pixel >> 5) & 0x3f) * 255 / 63), (unsigned char)((pixel & 0x1f) * 255 / 31) };
				file.write((const char*)rgb, 3);
			}
		}
		return (bool)file;
	}

	void log(const std::string& line)
	{
		std::lock_guard<std::mutex> lock(gLogMutex);
		gLog.push_back(line);
		printf("    [game log] %s\n", line.c_str());
	}
	const std::vector<std::string>& getLog()	{ return gLog; }
}

// The 3DS clock and the SD card, via the linker's "--wrap"
extern "C" time_t __wrap_time(time_t* out)
{
	const time_t now = gClockBase + (time_t)secondsSinceStart();
	if (out)
		*out = now;
	return now;
}
extern "C" ssize_t __real_write(int fd, const void* buffer, size_t count);
extern "C" ssize_t __wrap_write(int fd, const void* buffer, size_t count)
{
	if (gSDCardDelayMs > 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(gSDCardDelayMs));
	return __real_write(fd, buffer, count);
}

// --- libctru stand-ins ---

u64 svcGetSystemTick()		{ return (u64)(secondsSinceStart() * SYSCLOCK_ARM11); }
void svcSleepThread(int64_t ns)	{ std::this_thread::sleep_for(std::chrono::nanoseconds(ns)); }
Result svcGetThreadPriority(s32* out, Handle)	{ *out = 0x30; return 0; }

struct ThreadInfo {};
Thread threadCreate(ThreadFunc entrypoint, void* arg, size_t, int, int, bool)
{
	std::thread(entrypoint, arg).detach();
	static ThreadInfo info;
	return &info;
}

void LightLock_Init(LightLock*)			{}
void LightLock_Lock(LightLock* lock)	{ lock->mMutex.lock(); }
void LightLock_Unlock(LightLock* lock)	{ lock->mMutex.unlock(); }
void LightEvent_Init(LightEvent* event, ResetType)	{ event->mSignaled = false; }
void LightEvent_Wait(LightEvent* event)
{
	std::unique_lock<std::mutex> lock(event->mMutex);
	event->mCondition.wait(lock, [event] { return event->mSignaled; });
	event->mSignaled = false;
}
void LightEvent_Signal(LightEvent* event)
{
	{
		std::lock_guard<std::mutex> lock(event->mMutex);
		event->mSignaled = true;
	}
	event->mCondition.notify_one();
}

u8* gfxGetFramebuffer(gfxScreen_t, gfx3dSide_t, u16* width, u16* height)
{
	*width = 240;
	*height = 320;
	return (u8*)gFramebuffer;
}
GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t)	{ return gFormat; }
void gfxSetScreenFormat(gfxScreen_t, GSPGPU_FramebufferFormat format)	{ gFormat = format; }
void gfxSetDoubleBuffering(gfxScreen_t, bool)	{}
void gfxScreenSwapBuffers(gfxScreen_t, bool)	{ ++gSwapCount; }
Result GSPGPU_FlushDataCache(const void*, u32)	{ return 0; }

void hidScanInput()
{
	gWasTouching = gTouching;
	gTouching = gNextTouching;
}
u32 hidKeysDown()	{ return (gTouching && !gWasTouching) ? KEY_TOUCH : 0; }
u32 hidKeysHeld()	{ return gTouching ? KEY_TOUCH : 0; }
u32 hidKeysUp()		{ return (!gTouching && gWasTouching) ? KEY_TOUCH : 0; }
void hidTouchRead(touchPosition* pos)
{
	// Like on the 3DS: 0 / 0 when not touching
	pos->px = gTouching ? (u16)gTouchX : 0;
	pos->py = gTouching ? (u16)gTouchY : 0;
}

void aptHook(aptHookCookie* cookie, aptHookFn callback, void* param)	{ cookie->callback = callback; cookie->param = param; }
bool aptIsActive()		{ return true; }
bool aptShouldClose()	{ return false; }

Result cfguInit()	{ return 0; }
void cfguExit()		{}
Result CFGU_SecureInfoGetRegion(u8* region)	{ *region = (u8)gRegion; return 0; }
Result CFGU_GetConfigInfoBlk2(u32 size, u32 blkID, void* outData)
{
	memset(outData, 0, size);
	if (blkID == 0x000a0000)
	{
		u16* name = (u16*)outData;
		for (size_t i = 0; i < gNickname.size() && i < 10; ++i)
			name[i] = gNickname[i];
		return 0;
	}
	if (blkID == 0x000a0001 && size >= 2)
	{
		((u8*)outData)[0] = (u8)gBirthdayMonth;
		((u8*)outData)[1] = (u8)gBirthdayDay;
		return 0;
	}
	return -1;
}
Result ptmuInit()	{ return 0; }
Result PTMU_GetBatteryLevel(u8* out)		{ *out = (u8)gBatteryLevel; return 0; }
Result PTMU_GetBatteryChargeState(u8* out)	{ *out = gCharging ? 1 : 0; return 0; }

// --- Engine stand-ins ---

namespace n3ds { namespace gpu {
	void setTint(uint8_t red, uint8_t green, uint8_t blue)	{ gTopTint = ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue; }
} }

std::string rmx::convertToUTF8(const std::wstring& string)	{ return std::string(string.begin(), string.end()); }
void rmx::UTF8Conversion::convertFromUTF8(const std::string& input, std::wstring& output)	{ output.assign(input.begin(), input.end()); }

namespace FTX
{
	bool FakeFileSystem::createDirectory(const std::wstring& path)
	{
		const std::string p = rmx::convertToUTF8(path);
		for (size_t pos = p.find('/', p.find(":/") + 2); pos != std::string::npos; pos = p.find('/', pos + 1))
			mkdir(p.substr(0, pos).c_str(), 0777);
		mkdir(p.c_str(), 0777);
		return true;
	}
	bool FakeFileSystem::exists(const std::wstring& path)	{ return access(rmx::convertToUTF8(path).c_str(), F_OK) == 0; }
	bool FakeFileSystem::saveFile(const std::wstring& path, const void* data, size_t size)
	{
		std::ofstream file(rmx::convertToUTF8(path), std::ios::binary);
		file.write((const char*)data, size);
		return (bool)file;
	}
	FakeFileSystem gFileSystem;
	FakeFileSystem* FileSystem = &gFileSystem;
}
