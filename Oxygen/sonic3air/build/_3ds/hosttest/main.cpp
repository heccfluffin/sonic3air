// Host test for the 3DS port's bottom screen, 3DS menu data, profile and background file writer.
// Runs the real 3DS code against a fake 3DS (fake3ds.cpp) and saves what the bottom screen shows as pictures.
// Usage: hosttest <scenario> <output folder>   (run.sh runs all scenarios, each in its own empty folder as the "SD card")

#include "fake/3ds.h"
#include "fake/oxygen/pch.h"
#include "fake3ds.h"

#include "oxygen/platform/3ds/N3DSBottomScreen.h"
#include "oxygen/platform/3ds/N3DSFileWriter.h"
#include "oxygen/platform/3ds/N3DSProfile.h"
#include "oxygen/platform/3ds/N3DSShop.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <unistd.h>

using Request = n3ds::bottomscreen::Request;

namespace
{
	std::string gOut;
	int gFailures = 0;
	int gChecks = 0;

	void check(bool condition, const std::string& what)
	{
		++gChecks;
		printf("  %s  %s\n", condition ? "ok  " : "FAIL", what.c_str());
		if (!condition)
			++gFailures;
	}

	struct Level { bool mSaveStatesAllowed = true; int mSlot = 1; bool mSlotHasState = true; } gLevel;

	// One frame of the game: what GameApp does on the 3DS each frame
	Request frame()
	{
		hidScanInput();
		const Request request = n3ds::bottomscreen::update(gLevel.mSaveStatesAllowed, gLevel.mSlot, gLevel.mSlotHasState);
		n3ds::filewriter::update();
		fake::advanceSeconds(1.0 / 60.0);
		return request;
	}

	void frames(int count)
	{
		for (int i = 0; i < count; ++i)
			frame();
	}

	void shot(const std::string& name)
	{
		fake::saveScreenshot(gOut + "/" + name + ".ppm");
		printf("  shot %s\n", name.c_str());
	}

	// Touch and release, a few frames each, like a quick tap; returns any request the bottom screen made
	Request tap(int x, int y)
	{
		Request result = Request::NONE;
		fake::setTouch(true, x, y);
		for (int i = 0; i < 3; ++i) { const Request r = frame(); if (r != Request::NONE) result = r; }
		fake::setTouch(false);
		for (int i = 0; i < 3; ++i) { const Request r = frame(); if (r != Request::NONE) result = r; }
		return result;
	}

	void ownAllThemes()
	{
		n3ds::shop::get().mOwnedThemes = (1u << n3ds::shop::_NUM_THEMES) - 1;
	}

	// --- Scenarios ---

	void clock24()
	{
		fake::setRegion(CFG_REGION_EUR);
		fake::setTime(2026, 11, 21, 9, 41);
		fake::setBattery(4, false);
		frames(5);
		shot("clock_24h_0941_battery4");
		fake::setTime(2026, 11, 21, 23, 59);
		fake::advanceSeconds(1.0);
		frames(40);
		shot("clock_24h_2359");
	}

	void clock12()
	{
		fake::setRegion(CFG_REGION_USA);
		fake::setTime(2026, 11, 21, 21, 5);
		fake::setBattery(1, true);
		frames(5);
		shot("clock_12h_0905pm_battery1_charging");
		fake::setTime(2026, 11, 21, 0, 7);
		fake::setBattery(0, false);
		fake::advanceSeconds(6.0);
		frames(40);
		shot("clock_12h_1207am_battery0");
		fake::setTime(2026, 11, 21, 12, 30);
		fake::setBattery(5, false);
		fake::advanceSeconds(6.0);
		frames(40);
		shot("clock_12h_1230pm_battery5");
	}

	void idleRedraws()
	{
		// Ten minutes of playing without touching anything: the bottom screen should only be redrawn when the minute changes
		fake::setTime(2026, 11, 21, 15, 0);
		frames(5);
		const int before = fake::getSwapCount();
		const auto start = std::chrono::steady_clock::now();
		frames(60 * 60 * 10);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		const int redraws = fake::getSwapCount() - before;
		check(redraws >= 9 && redraws <= 11, "10 minutes idle: " + std::to_string(redraws) + " bottom screen redraws (expected about 10, one per minute)");
		printf("  info  36000 frames of bottom screen updates took %.1f ms on this PC (%.2f us per frame)\n", ms, ms * 1000.0 / 36000);

		fake::setBattery(2, false);
		const int beforeBattery = fake::getSwapCount();
		frames(6 * 60);
		check(fake::getSwapCount() > beforeBattery, "battery change shows up within 6 seconds");
		shot("battery2_yellow");
	}

	void timeOfDay()
	{
		ownAllThemes();
		n3ds::shop::get().mSelectedTheme = n3ds::shop::THEME_AUTO;
		const struct { int mHour; int mMinute; const char* mName; int mExpected; } TIMES[] =
		{
			{ 7, 0, "0700_morning", n3ds::shop::THEME_AURORA },
			{ 12, 0, "1200_day", n3ds::shop::THEME_BLUE },
			{ 18, 0, "1800_evening", n3ds::shop::THEME_SUNSET },
			{ 23, 0, "2300_night", n3ds::shop::THEME_NIGHT },
			{ 3, 0, "0300_night", n3ds::shop::THEME_NIGHT },
		};
		bool first = true;
		for (const auto& t : TIMES)
		{
			fake::setTime(2026, 11, 21, t.mHour, t.mMinute);
			check(n3ds::shop::resolveTheme(n3ds::shop::THEME_AUTO) == t.mExpected, std::string("Time of Day at ") + t.mName + " picks theme " + std::to_string(t.mExpected));
			if (first)
			{
				frames(5);		// Starts with the theme for the time
				first = false;
			}
			fake::advanceSeconds(1.0);
			frames(40);
			shot(std::string("timeofday_") + t.mName);
		}

		// Switching by itself while playing: 16:59:50 -> 17:00
		fake::setTime(2026, 11, 21, 16, 59);
		fake::advanceSeconds(50);
		frames(40);
		shot("timeofday_1659_before_switch");
		frames(60 * 15);
		shot("timeofday_1700_after_switch");

		// Owning none of the extra themes: always Blue
		n3ds::shop::get().mOwnedThemes = (1u << n3ds::shop::THEME_BLUE);
		bool allBlue = true;
		for (int hour = 0; hour < 24; ++hour)
		{
			fake::setTime(2026, 11, 21, hour, 30);
			allBlue = allBlue && (n3ds::shop::resolveTheme(n3ds::shop::THEME_AUTO) == n3ds::shop::THEME_BLUE);
		}
		check(allBlue, "Time of Day without any bought themes stays Blue all day");

		// Only Sunset owned: used in the evening and at night
		n3ds::shop::get().mOwnedThemes = (1u << n3ds::shop::THEME_BLUE) | (1u << n3ds::shop::THEME_SUNSET);
		fake::setTime(2026, 11, 21, 23, 0);
		check(n3ds::shop::resolveTheme(n3ds::shop::THEME_AUTO) == n3ds::shop::THEME_SUNSET, "Time of Day with only Sunset owned uses it at night too");
		fake::setTime(2026, 11, 21, 7, 0);
		check(n3ds::shop::resolveTheme(n3ds::shop::THEME_AUTO) == n3ds::shop::THEME_BLUE, "... and Blue in the morning (Aurora not owned)");
	}

	void nightMode()
	{
		ownAllThemes();
		n3ds::shop::get().mSelectedTheme = n3ds::shop::THEME_SEA;
		fake::setTime(2026, 11, 21, 19, 59);
		frames(5);
		shot("nightmode_off_sea");
		n3ds::shop::get().mNightMode = n3ds::shop::NIGHT_MODE_ALWAYS;
		frames(40);
		shot("nightmode_always_sea");

		n3ds::shop::get().mNightMode = n3ds::shop::NIGHT_MODE_AFTER_DARK;
		frames(40);
		check(!n3ds::shop::isNightModeActive(), "after dark: not active at 19:59");
		fake::advanceSeconds(61);
		frames(40);
		check(n3ds::shop::isNightModeActive(), "after dark: active at 20:00");
		shot("nightmode_afterdark_2000_sea");
		fake::setTime(2026, 11, 21, 6, 0);
		check(!n3ds::shop::isNightModeActive(), "after dark: off again at 6:00");
		fake::setTime(2026, 11, 21, 5, 59);
		check(n3ds::shop::isNightModeActive(), "after dark: still on at 5:59");
	}

	void taps()
	{
		fake::setTime(2026, 11, 21, 14, 20);
		frames(5);

		// Tap everywhere (every 8 pixels) and see where the profiler opens
		int openedInside = 0, openedOutside = 0, requests = 0;
		std::string outsidePoints;
		gLevel.mSaveStatesAllowed = false;		// So SAVE / LOAD don't start anything
		for (int y = 1; y < 240; y += 8)
		{
			for (int x = 1; x < 320; x += 8)
			{
				if (tap(x, y) != Request::NONE)
					++requests;
				if (n3ds::bottomscreen::isShowingStats())
				{
					const bool inside = (x >= 252 && x < 318 && y >= 2 && y < 32);
					(inside ? openedInside : openedOutside)++;
					if (!inside)
						outsidePoints += " (" + std::to_string(x) + "," + std::to_string(y) + ")";
					tap(160, 120);		// Back to the controls
					check(!n3ds::bottomscreen::isShowingStats(), "tapping again closes the statistics") ;
				}
			}
		}
		check(openedInside > 0, "STATS button opens the statistics (" + std::to_string(openedInside) + " tap points)");
		check(openedOutside == 0, "no tap outside the STATS button opens them" + (outsidePoints.empty() ? std::string() : (": " + outsidePoints)));
		check(requests == 0, "outside of levels, no tap starts a save or load");

		// In a level: SAVE and LOAD
		gLevel.mSaveStatesAllowed = true;
		gLevel.mSlotHasState = false;
		frames(5);
		shot("level_slot1_no_state");
		fake::setTouch(true, 260, 70);
		frames(3);
		shot("save_pressed");
		fake::setTouch(false);
		Request request = frame();
		shot("saving");
		if (request == Request::NONE)
			request = frame();
		check(request == Request::SAVE_STATE, "tapping SAVE asks for a save state (after showing 'Saving...')");
		n3ds::bottomscreen::showResult(n3ds::bottomscreen::Result::SAVED);
		gLevel.mSlotHasState = true;
		frames(5);
		shot("state_saved");
		frames(60 * 3);
		shot("level_slot1_has_state");
		check(tap(260, 150) == Request::LOAD_STATE, "tapping LOAD asks to load the state");

		// Sliding off a button cancels
		fake::setTouch(true, 260, 70);
		frames(3);
		fake::setTouch(true, 100, 70);
		frames(3);
		fake::setTouch(false);
		const Request slid = frame();
		check(slid == Request::NONE && frame() == Request::NONE, "sliding off SAVE before letting go does nothing");
	}

	void greeting()
	{
		const struct { int mHour; const char* mExpected; } TIMES[] =
		{
			{ 4, "Playing late, Tester? Don't forget to rest!" },
			{ 5, "Good morning, Tester!" },
			{ 11, "Good morning, Tester!" },
			{ 12, "Good afternoon, Tester!" },
			{ 17, "Good evening, Tester!" },
			{ 21, "Good evening, Tester!" },
			{ 22, "Playing late, Tester? Don't forget to rest!" },
		};
		for (const auto& t : TIMES)
		{
			fake::setTime(2026, 11, 21, t.mHour, 15);
			const std::string greeting = n3ds::profile::getGreeting();
			check(greeting == t.mExpected, std::to_string(t.mHour) + ":15 -> \"" + greeting + "\"");
		}
	}

	void greetingBirthday()
	{
		fake::setBirthday(11, 21);
		fake::setTime(2026, 11, 21, 15, 0);
		check(n3ds::profile::getGreeting() == "Happy birthday, Tester!", "on the birthday: \"" + n3ds::profile::getGreeting() + "\"");
		fake::setTime(2026, 11, 22, 15, 0);
		check(n3ds::profile::getGreeting() == "Good afternoon, Tester!", "the day after: \"" + n3ds::profile::getGreeting() + "\"");
	}

	void greetingUnusualName()
	{
		fake::setNickname(u"Zoë");		// "Zoë": the game's fonts can't show the "ë"
		fake::setTime(2026, 11, 21, 9, 0);
		check(n3ds::profile::getGreeting() == "Good morning!", "name the fonts can't show is left out: \"" + n3ds::profile::getGreeting() + "\"");
	}

	std::string readFile(const std::string& path)
	{
		std::ifstream file(path, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	}

	void fileWriter()
	{
		fake::setSDCardDelay(1500);		// Like the slow SD card seen in the log: 1.5 s per write
		const std::string path = "sdmc:/3ds/sonic3air/savedata/test_save.bin";

		auto start = std::chrono::steady_clock::now();
		bool firstDone = false;
		n3ds::filewriter::writeFile(path, std::vector<uint8_t>(100 * 1024, 'A'), [&](bool success, uint32_t) { firstDone = success; });
		double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		check(ms < 50, "queuing a 100 KB write returns right away (" + std::to_string((int)ms) + " ms) while the SD card takes 1.5 s");
		check(n3ds::filewriter::isBusy(), "... and the writer is busy in the background");

		// While the first write is running: two more of another file, the second replaces the first
		svcSleepThread(100 * 1000 * 1000);
		int secondCallbacks = 0, thirdCallbacks = 0;
		const std::string path2 = "sdmc:/3ds/sonic3air/savedata/other.txt";
		n3ds::filewriter::writeFile(path2, std::vector<uint8_t>{ 'o', 'l', 'd' }, [&](bool, uint32_t) { ++secondCallbacks; });
		n3ds::filewriter::writeFile(path2, std::vector<uint8_t>{ 'n', 'e', 'w' }, [&](bool, uint32_t) { ++thirdCallbacks; });

		// The game keeps running at 60 fps meanwhile
		start = std::chrono::steady_clock::now();
		int framesRun = 0;
		while (n3ds::filewriter::isBusy() && framesRun < 600)
		{
			n3ds::filewriter::update();
			svcSleepThread(16 * 1000 * 1000);
			++framesRun;
		}
		ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		printf("  info  game ran %d frames during %.0f ms of background writing\n", framesRun, ms);
		n3ds::filewriter::waitUntilDone();
		check(firstDone, "first write finished successfully");
		check(readFile(path).size() == 100 * 1024, "first file has all 100 KB");
		check(readFile(path2) == "new", "replaced write: the file has the newest content ('" + readFile(path2) + "')");
		check(secondCallbacks == 0 && thirdCallbacks == 1, "only the newest write reports back (" + std::to_string(secondCallbacks) + " / " + std::to_string(thirdCallbacks) + ")");
		bool slowLogged = false;
		for (const std::string& line : fake::getLog())
			slowLogged = slowLogged || (line.find("slow SD card") != std::string::npos);
		check(slowLogged, "the slow write is noted in the log");

		// Missing folders get created
		fake::setSDCardDelay(0);
		n3ds::filewriter::writeFile("sdmc:/3ds/sonic3air/new/folder/file.txt", std::vector<uint8_t>{ 'h', 'i' });
		n3ds::filewriter::waitUntilDone();
		check(readFile("sdmc:/3ds/sonic3air/new/folder/file.txt") == "hi", "writing into missing folders creates them");

		// The 3DS menu file
		n3ds::shop::get().mNightMode = n3ds::shop::NIGHT_MODE_AFTER_DARK;
		n3ds::shop::get().mSelectedTheme = n3ds::shop::THEME_AUTO;
		n3ds::shop::save();
		n3ds::filewriter::waitUntilDone();
		const std::string shopFile = readFile("sdmc:/3ds/sonic3air/savedata/3ds_shop.txt");
		check(shopFile.find("theme=101\n") != std::string::npos && shopFile.find("night_mode=1\n") != std::string::npos, "3DS menu choices are saved (theme=101, night_mode=1)");
	}

	void shopReload()
	{
		// Reading back what "fileWriter" saved: Time of Day and night mode survive a restart
		FTX::FileSystem->createDirectory(L"sdmc:/3ds/sonic3air/savedata");
		std::ofstream("sdmc:/3ds/sonic3air/savedata/3ds_shop.txt") << "owned_themes=1\ntheme=101\nowns_shield=0\nshield=0\nshow_menu=1\nnight_mode=2\n";
		check(n3ds::shop::get().mSelectedTheme == n3ds::shop::THEME_AUTO, "Time of Day is kept after a restart");
		check(n3ds::shop::get().mNightMode == n3ds::shop::NIGHT_MODE_ALWAYS, "night mode is kept after a restart");
	}
}

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		printf("Usage: hosttest <scenario> <output folder>\n");
		return 2;
	}
	const std::string scenario = argv[1];
	gOut = argv[2];

	static const std::map<std::string, void(*)()> SCENARIOS =
	{
		{ "clock24", clock24 }, { "clock12", clock12 }, { "idle", idleRedraws }, { "timeofday", timeOfDay }, { "nightmode", nightMode },
		{ "taps", taps }, { "greeting", greeting }, { "birthday", greetingBirthday }, { "unusualname", greetingUnusualName },
		{ "filewriter", fileWriter }, { "shopreload", shopReload },
	};
	const auto it = SCENARIOS.find(scenario);
	if (it == SCENARIOS.end())
	{
		printf("Unknown scenario '%s'\n", scenario.c_str());
		return 2;
	}
	it->second();
	printf("  %d checks, %d failed\n", gChecks, gFailures);
	fflush(stdout);
	_exit(gFailures == 0 ? 0 : 1);		// Without waiting for the writer thread, which runs forever (detached, like on the 3DS)
}
