// Host test for the 3DS port's bottom screen, 3DS menu data, profile and background file writer.
// Runs the real 3DS code against a fake 3DS (fake3ds.cpp) and saves what the bottom screen shows as pictures.
// Usage: hosttest <scenario> <output folder>   (run.sh runs all scenarios, each in its own empty folder as the "SD card")

#include "fake/3ds.h"
#include "fake/oxygen/pch.h"
#include "fake3ds.h"

#include "oxygen/platform/3ds/N3DSBottomScreen.h"
#include "oxygen/platform/3ds/N3DSChallenges.h"
#include "oxygen/platform/3ds/N3DSFileWriter.h"
#include "oxygen/platform/3ds/N3DSGameBanana.h"
#include "oxygen/platform/3ds/N3DSJournal.h"
#include "oxygen/platform/3ds/N3DSModServer.h"
#include "oxygen/platform/3ds/N3DSQRCode.h"
#include "oxygen/platform/3ds/N3DSProfile.h"
#include "oxygen/platform/3ds/N3DSShop.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <thread>
#include <map>
#include <memory>
#include <unistd.h>
#include <arpa/inet.h>

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
		check(n3ds::profile::formatTime(20 * 60) == "8:00 PM" && n3ds::profile::formatTime(30) == "12:30 AM" && n3ds::profile::formatTime(12 * 60) == "12:00 PM",
			  "times shown as 8:00 PM / 12:30 AM / 12:00 PM on an American 3DS");
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
		check(fake::getTopScreenTint() == 0xffffff, "night mode off: the top screen has no tint");
		n3ds::shop::get().mNightMode = n3ds::shop::NIGHT_MODE_ALWAYS;
		frames(40);
		shot("nightmode_always_sea");
		check(fake::getTopScreenTint() != 0xffffff, "night mode on: the top screen gets the warm tint");

		n3ds::shop::get().mNightMode = n3ds::shop::NIGHT_MODE_AFTER_DARK;
		frames(40);
		check(!n3ds::shop::isNightModeActive(), "after dark: not active at 19:59");
		fake::advanceSeconds(61);
		frames(40);
		check(n3ds::shop::isNightModeActive(), "after dark: active at 20:00");
		check(fake::getTopScreenTint() != 0xffffff, "... with the warm tint on the top screen too");
		shot("nightmode_afterdark_2000_sea");
		fake::setTime(2026, 11, 21, 6, 0);
		check(!n3ds::shop::isNightModeActive(), "after dark: off again at 6:00");
		fake::setTime(2026, 11, 21, 5, 59);
		check(n3ds::shop::isNightModeActive(), "after dark: still on at 5:59");

		// Own times: a night owl, 22:30 to 7:00 (over midnight)
		n3ds::shop::get().mNightStart = 22 * 60 + 30;
		n3ds::shop::get().mNightEnd = 7 * 60;
		fake::setTime(2026, 11, 21, 22, 29);
		check(!n3ds::shop::isNightModeActive(), "own times 22:30 - 7:00: off at 22:29");
		fake::setTime(2026, 11, 21, 22, 30);
		check(n3ds::shop::isNightModeActive(), "own times 22:30 - 7:00: on at 22:30");
		fake::setTime(2026, 11, 21, 6, 59);
		check(n3ds::shop::isNightModeActive(), "own times 22:30 - 7:00: still on at 6:59");
		fake::setTime(2026, 11, 21, 7, 0);
		check(!n3ds::shop::isNightModeActive(), "own times 22:30 - 7:00: off at 7:00");

		// Not over midnight: 1:00 to 5:30
		n3ds::shop::get().mNightStart = 1 * 60;
		n3ds::shop::get().mNightEnd = 5 * 60 + 30;
		fake::setTime(2026, 11, 21, 0, 59);
		const bool before = n3ds::shop::isNightModeActive();
		fake::setTime(2026, 11, 21, 3, 0);
		const bool during = n3ds::shop::isNightModeActive();
		fake::setTime(2026, 11, 21, 5, 30);
		const bool after = n3ds::shop::isNightModeActive();
		check(!before && during && !after, "own times 1:00 - 5:30: off at 0:59, on at 3:00, off at 5:30");

		n3ds::shop::get().mNightStart = n3ds::shop::get().mNightEnd = 9 * 60;
		check(!n3ds::shop::isNightModeActive(), "start = end: never on (instead of always)");
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

		// Screenshot (L + R): its message on the bottom screen
		n3ds::bottomscreen::showResult(n3ds::bottomscreen::Result::SCREENSHOT_SAVED);
		frames(5);
		shot("screenshot_saved");

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

		// A background job queued after a write sees the written file (they run in order)
		fake::setSDCardDelay(300);
		n3ds::filewriter::writeFile(path2, std::vector<uint8_t>{ 'l', 'a', 't', 'e', 's', 't' });
		auto seen = std::make_shared<std::string>();
		bool jobDone = false;
		start = std::chrono::steady_clock::now();
		n3ds::filewriter::runInBackground("reading back", [seen, path2]() { *seen = readFile(path2); }, [&jobDone]() { jobDone = true; });
		ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		check(ms < 50, "queuing a background job returns right away (" + std::to_string((int)ms) + " ms)");
		n3ds::filewriter::waitUntilDone();
		check(jobDone && *seen == "latest", "a background read after a write sees the new content ('" + *seen + "'), and reports back on the main thread");

		// Missing folders get created
		fake::setSDCardDelay(0);
		n3ds::filewriter::writeFile("sdmc:/3ds/sonic3air/new/folder/file.txt", std::vector<uint8_t>{ 'h', 'i' });
		n3ds::filewriter::waitUntilDone();
		check(readFile("sdmc:/3ds/sonic3air/new/folder/file.txt") == "hi", "writing into missing folders creates them");

		// The same content again isn't written again (the file is removed in between, so a write would bring it back)
		{
			std::vector<uint8_t> same = { 's', 'a', 'm', 'e' };
			n3ds::filewriter::writeFileIfOnSDCard(L"sdmc:/3ds/sonic3air/same.txt", same);
			n3ds::filewriter::waitUntilDone();
			std::remove("sdmc:/3ds/sonic3air/same.txt");
			std::vector<uint8_t> again = { 's', 'a', 'm', 'e' };
			n3ds::filewriter::writeFileIfOnSDCard(L"sdmc:/3ds/sonic3air/same.txt", again);
			n3ds::filewriter::waitUntilDone();
			check(readFile("sdmc:/3ds/sonic3air/same.txt").empty(), "unchanged content: not written again");
			std::vector<uint8_t> changed = { 'n', 'e', 'w' };
			n3ds::filewriter::writeFileIfOnSDCard(L"sdmc:/3ds/sonic3air/same.txt", changed);
			n3ds::filewriter::waitUntilDone();
			check(readFile("sdmc:/3ds/sonic3air/same.txt") == "new", "changed content: written");
		}

		// Quick changes to a small file (like options changed one after the other): one write a second later, with the newest
		// content - and written over in place, no temporary file
		{
			std::vector<uint8_t> first = { '1' };
			n3ds::filewriter::writeFileIfOnSDCard(L"sdmc:/3ds/sonic3air/quick.txt", first);
			n3ds::filewriter::waitUntilDone();
			const int callsBefore = fake::getWriteCalls();
			for (char c : std::string("2345"))
			{
				std::vector<uint8_t> next = { (uint8_t)c, (uint8_t)c };
				n3ds::filewriter::writeFileIfOnSDCard(L"sdmc:/3ds/sonic3air/quick.txt", next);
				std::this_thread::sleep_for(std::chrono::milliseconds(150));
			}
			check(readFile("sdmc:/3ds/sonic3air/quick.txt") == "1", "quick changes to a small file wait a moment before being written");
			for (int k = 0; k < 40 && fake::getWriteCalls() == callsBefore; ++k)
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			check(fake::getWriteCalls() - callsBefore == 1 && readFile("sdmc:/3ds/sonic3air/quick.txt") == "55",
				  "... then they're one write, with the newest content (" + std::to_string(fake::getWriteCalls() - callsBefore) + " write, '" + readFile("sdmc:/3ds/sonic3air/quick.txt") + "')");
			struct stat quickInfo;
			check(stat("sdmc:/3ds/sonic3air/quick.txt.new", &quickInfo) != 0, "... written over in place (it was there already)");
		}

		// Written under another name first, then put in place; interrupted writes get sorted out at the next start
		struct stat info;
		check(stat("sdmc:/3ds/sonic3air/new/folder/file.txt.new", &info) != 0, "no temporary file left behind after writing");
		std::ofstream("sdmc:/3ds/sonic3air/new/folder/file.txt.new") << "half";		// Cut off while writing: the old file is still there
		std::ofstream("sdmc:/3ds/sonic3air/new/folder/other.txt.new") << "complete";	// Cut off after removing the old file
		n3ds::filewriter::recoverInterruptedWrites("sdmc:/3ds/sonic3air/new/folder/");
		check(readFile("sdmc:/3ds/sonic3air/new/folder/file.txt") == "hi" && stat("sdmc:/3ds/sonic3air/new/folder/file.txt.new", &info) != 0,
			  "an unfinished write next to its file gets discarded, the old file stays");
		check(readFile("sdmc:/3ds/sonic3air/new/folder/other.txt") == "complete" && stat("sdmc:/3ds/sonic3air/new/folder/other.txt.new", &info) != 0,
			  "a finished write whose old file was already removed takes its place");

		// The 3DS menu file
		n3ds::shop::get().mNightMode = n3ds::shop::NIGHT_MODE_AFTER_DARK;
		n3ds::shop::get().mSelectedTheme = n3ds::shop::THEME_AUTO;
		n3ds::shop::save();
		n3ds::filewriter::waitUntilDone();
		const std::string shopFile = readFile("sdmc:/3ds/sonic3air/savedata/3ds_shop.txt");
		check(shopFile.find("theme=101\n") != std::string::npos && shopFile.find("night_mode=1\n") != std::string::npos, "3DS menu choices are saved (theme=101, night_mode=1)");
		check(shopFile.find("night_start=1200\n") != std::string::npos && shopFile.find("night_end=360\n") != std::string::npos, "night times are saved too (defaults 20:00 / 6:00)");
	}

	void shopReload()
	{
		// Reading back what "fileWriter" saved: Time of Day and night mode survive a restart
		FTX::FileSystem->createDirectory(L"sdmc:/3ds/sonic3air/savedata");
		std::ofstream("sdmc:/3ds/sonic3air/savedata/3ds_shop.txt") << "owned_themes=1\ntheme=101\nowns_shield=0\nshield=0\nshow_menu=1\nnight_mode=2\nnight_start=1350\nnight_end=420\n";
		check(n3ds::shop::get().mSelectedTheme == n3ds::shop::THEME_AUTO, "Time of Day is kept after a restart");
		check(n3ds::shop::get().mNightMode == n3ds::shop::NIGHT_MODE_ALWAYS, "night mode is kept after a restart");
		check(n3ds::shop::get().mNightStart == 22 * 60 + 30 && n3ds::shop::get().mNightEnd == 7 * 60, "own night times are kept after a restart");
		check(n3ds::profile::formatTime(22 * 60 + 30) == "22:30", "times shown as 22:30 on a European 3DS");
	}

	void writeJournal(const std::string& text)
	{
		FTX::FileSystem->createDirectory(L"sdmc:/3ds/sonic3air/savedata");
		std::ofstream("sdmc:/3ds/sonic3air/savedata/3ds_journal.txt") << text;
	}

	void postcards()
	{
		// A journal from before postcards: 3 cards completed, and the 7th stamp of the 4th card due today
		fake::setTime(2026, 11, 21, 21, 0);
		writeJournal("started=20261001\nlast_day=20261120\ndays_played=27\nstamps=6\ncards_completed=3\nplay_seconds=36000\n");
		const n3ds::journal::Data& journal = n3ds::journal::get();
		check(journal.mPostcards.size() == 3, "3 cards completed before there were postcards: 3 postcards arrive (" + std::to_string(journal.mPostcards.size()) + ")");
		check(n3ds::journal::takeNewPostcardNotice(), "... with the notice for the 3DS menu");
		bool distinct = true;
		for (size_t i = 0; i < journal.mPostcards.size(); ++i)
			for (size_t k = 0; k < i; ++k)
				distinct = distinct && (journal.mPostcards[i].mFunny >= 0 || journal.mPostcards[k].mFunny >= 0 || journal.mPostcards[i].mZone != journal.mPostcards[k].mZone);
		check(distinct, "... the zone ones each from a different zone");

		for (int i = 0; i < 4 * 60; ++i)
		{
			frame();
			n3ds::journal::update();
		}
		check(journal.mStamps == 7 && journal.mCardsCompleted == 4, "today's stamp completes the 4th card");
		check(journal.mPostcards.size() == 4 && journal.mPostcards.back().mDate == 20261121, "... and its postcard arrives, dated today");
		n3ds::filewriter::waitUntilDone();
		check(readFile("sdmc:/3ds/sonic3air/savedata/3ds_journal.txt").find("stamps=7") == std::string::npos,
			  "the stamp isn't written right away (a slow SD card froze the title screen while it was)");
		for (int i = 0; i < 15 * 60; ++i)
		{
			frame();
			n3ds::journal::update();
		}
		n3ds::filewriter::waitUntilDone();
		check(readFile("sdmc:/3ds/sonic3air/savedata/3ds_journal.txt").find("stamps=7") != std::string::npos, "... but 15 seconds later");
		frames(60);
		shot("badges_4_cards_24h");

		n3ds::journal::save();
		n3ds::filewriter::waitUntilDone();
		const std::string saved = readFile("sdmc:/3ds/sonic3air/savedata/3ds_journal.txt");
		size_t lines = 0;
		for (size_t pos = saved.find("\npostcard="); pos != std::string::npos; pos = saved.find("\npostcard=", pos + 1))
			++lines;
		check(lines == 4, "the postcards are saved in the journal file (" + std::to_string(lines) + " lines)");
		check(saved.find("postcard=" + journal.mPostcards.back().getSpriteName() + " 20261121\n") != std::string::npos, "... like \"postcard=" + journal.mPostcards.back().getSpriteName() + " 20261121\"");
	}

	void rewards()
	{
		// A journal from before vouchers and bonus boxes: the latest of its 3 cards gives one of each, as a welcome
		fake::setTime(2026, 11, 21, 21, 0);
		writeJournal("started=20261001\nlast_day=20261120\ndays_played=27\nstamps=6\ncards_completed=3\nplay_seconds=36000\n"
					 "postcard=zone01_act1a 20261008\npostcard=zone02_act1a 20261015\npostcard=funny_decoys 20261022\n");
		const n3ds::journal::Data& journal = n3ds::journal::get();
		check(journal.mVouchers == 1 && journal.mBonusBoxes == 1 && journal.mRewardedCards == 3, "an older journal with 3 cards gets 1 voucher and 1 bonus box");
		check(n3ds::journal::takeNewRewardNotice() && !n3ds::journal::takeNewRewardNotice(), "... with a notice for the 3DS menu, once");
		check(journal.mPostcards.size() == 3, "... and no extra postcards");

		check(n3ds::journal::giveWelcomePostcard() && journal.mPostcards.size() == 4, "the welcome postcard arrives the first time the journal is opened");
		check(!n3ds::journal::giveWelcomePostcard() && journal.mPostcards.size() == 4, "... only once");

		check(n3ds::journal::prepareBonus(n3ds::journal::BONUS_FIRE_SHIELD) && journal.mBonusBoxes == 0 && journal.mBonusReady == n3ds::journal::BONUS_FIRE_SHIELD,
			  "opening the bonus box with a fire shield in it");
		check(n3ds::journal::prepareBonus(n3ds::journal::BONUS_RINGS) && journal.mBonusReady == n3ds::journal::BONUS_RINGS, "... changing it to rings doesn't need another box");
		n3ds::journal::bonusGiven();
		check(journal.mBonusReady == n3ds::journal::BONUS_NONE, "given in the game: nothing ready any more");
		check(!n3ds::journal::prepareBonus(n3ds::journal::BONUS_FIRE_SHIELD), "no box left to open");
		check(n3ds::journal::useVoucher() && journal.mVouchers == 0 && !n3ds::journal::useVoucher(), "the voucher can be used once");

		for (int i = 0; i < 4 * 60; ++i)
		{
			frame();
			n3ds::journal::update();
		}
		check(journal.mCardsCompleted == 4 && journal.mPostcards.size() == 5, "the 4th card still gets its own postcard after the welcome one (" + std::to_string(journal.mPostcards.size()) + ")");
		check(journal.mVouchers == 1 && journal.mBonusBoxes == 1, "... and a voucher and a bonus box");

		for (int i = 0; i < 100; ++i)
			n3ds::journal::addGameDistance(6 * 256);		// 100 frames at Sonic's top speed: 600 pixels
		check(journal.mGameSteps == 600 / n3ds::journal::GAME_STEP_PIXELS, "Sonic's steps: one per " + std::to_string(n3ds::journal::GAME_STEP_PIXELS) + " pixels (" + std::to_string(journal.mGameSteps) + ")");

		n3ds::journal::save();
		n3ds::filewriter::waitUntilDone();
		const std::string saved = readFile("sdmc:/3ds/sonic3air/savedata/3ds_journal.txt");
		check(saved.find("game_steps=25\n") != std::string::npos, "... saved in the journal file");
		check(saved.find("vouchers=1\n") != std::string::npos && saved.find("bonus_boxes=1\n") != std::string::npos && saved.find("rewarded_cards=4\n") != std::string::npos &&
			  saved.find("welcome_postcard=1\n") != std::string::npos, "rewards are saved in the journal file");
	}

	void rewardsReload()
	{
		// Read back: nothing given twice, and an opened box stays ready
		fake::setTime(2026, 11, 22, 10, 0);
		writeJournal("started=20261001\nlast_day=20261122\ndays_played=28\nstamps=1\ncards_completed=4\nrewarded_cards=4\nvouchers=2\nbonus_boxes=0\nbonus_ready=3\nwelcome_postcard=1\n"
					 "postcard=zone01_act1a 20261008\npostcard=zone02_act1a 20261015\npostcard=funny_decoys 20261022\npostcard=zone03_act1a 20261121\npostcard=zone04_act1a 20261121\n");
		const n3ds::journal::Data& journal = n3ds::journal::get();
		check(journal.mVouchers == 2 && journal.mBonusBoxes == 0 && journal.mBonusReady == n3ds::journal::BONUS_LIGHTNING_SHIELD, "vouchers and the ready bonus are read back");
		check(!n3ds::journal::takeNewRewardNotice() && journal.mPostcards.size() == 5, "nothing is given twice");
		check(!n3ds::journal::giveWelcomePostcard(), "no second welcome postcard");
	}

	bool actDone(uint8_t zone, uint8_t act, uint16_t rings, uint32_t seconds, bool wasHit, bool lowOnAir = false, uint8_t characters = 1)
	{
		n3ds::challenges::ActResult result;
		result.mZone = zone;
		result.mAct = act;
		result.mRings = rings;
		result.mSeconds = seconds;
		result.mWasHit = wasHit;
		result.mLowOnAir = lowOnAir;
		result.mCharacters = characters;
		return n3ds::challenges::onActCompleted(result);
	}

	void challenges()
	{
		using namespace n3ds::challenges;
		FTX::FileSystem->createDirectory(L"sdmc:/3ds/sonic3air/savedata");

		// Today's challenges, set up by hand: 50 rings in Hydrocity, Angel Island 1 in under 2:30, Hydrocity without a hit
		fake::setTime(2026, 11, 23, 18, 0);
		std::ofstream("sdmc:/3ds/sonic3air/savedata/3ds_challenges.txt") << "date=20261123\nxp=40\nbeaten=9\nperfect_days=2\nzones=15\n"
			"challenge=0 1 0 50 0 0\nchallenge=4 0 0 150 0 0\nchallenge=6 1 0 0 0 0\n";
		const Data& data = get(15);
		check(data.mDate == 20261123 && data.mXp == 40 && data.mChallenges[0].mType == Type::RINGS_IN_ZONE, "today's challenges are read back");
		check(describe(data.mChallenges[0]) == "Collect 50 rings in Hydrocity" && describe(data.mChallenges[1]) == "Angel Island 1 in under 2:30" &&
			  describe(data.mChallenges[2]) == "Clear Hydrocity without a hit", "... and described (" + describe(data.mChallenges[1]) + ")");

		Frame frame;
		frame.mZone = 0;
		frame.mRings = 60;
		onFrame(frame);
		check(!data.mChallenges[0].mDone && data.mChallenges[0].mProgress == 0, "60 rings in Angel Island don't count for Hydrocity");
		frame.mZone = 1;
		frame.mRings = 49;
		onFrame(frame);
		check(!data.mChallenges[0].mDone && describeProgress(data.mChallenges[0]) == "49 / 50 rings", "49 rings in Hydrocity: not yet (" + describeProgress(data.mChallenges[0]) + ")");
		frame.mRings = 50;
		onFrame(frame);
		check(data.mChallenges[0].mDone && data.mXp == 50 && getLevel(data.mXp) == 2, "50 rings: beaten, 10 XP, medal level 2 (at 50 XP)");
		check(takeBeatenNotice() == 0 && takeLevelUpNotice() && n3ds::journal::get().mVouchers == 1, "... with a Robotnik voucher for the new level");

		setPinned(0);
		check(data.mPinned == -1, "a beaten challenge can't be pinned");
		setPinned(1);
		check(data.mPinned == 1, "pinning Angel Island 1 in under 2:30");
		actDone(0, 0, 10, 160, false);
		check(!data.mChallenges[1].mDone && describeProgress(data.mChallenges[1]) == "Best: 2:40", "Angel Island 1 in 2:40 is too slow");
		actDone(0, 1, 10, 100, false);
		check(!data.mChallenges[1].mDone, "... and act 2 doesn't count");
		actDone(0, 0, 10, 140, false);
		check(data.mChallenges[1].mDone && data.mXp == 70, "2:20: beaten, 20 XP");
		check(data.mPinned == -1, "... and beating it unpins it");

		actDone(1, 1, 0, 100, true);
		check(!data.mChallenges[2].mDone, "a hit in Hydrocity: not beaten");
		actDone(1, 0, 0, 100, false);
		check(data.mChallenges[2].mDone && data.mXp == 100 && data.mPerfectDays == 3 && data.mBeaten == 12, "without a hit: beaten, 30 XP, a perfect day");

		n3ds::filewriter::waitUntilDone();
		const std::string saved = readFile("sdmc:/3ds/sonic3air/savedata/3ds_challenges.txt");
		check(saved.find("xp=100\n") != std::string::npos && saved.find("challenge=6 1 0 0 0 1\n") != std::string::npos, "saved right away");

		// The next day: new challenges, the medal stays
		fake::setTime(2026, 11, 24, 9, 0);
		get();
		check(data.mDate == 20261124 && data.mXp == 100 && !data.mChallenges[0].mDone && !data.mChallenges[1].mDone && !data.mChallenges[2].mDone,
			  "a new day brings new challenges: " + describe(data.mChallenges[0]) + " / " + describe(data.mChallenges[1]) + " / " + describe(data.mChallenges[2]));

		// Only zones already reached: with just Angel Island 1 finished, every challenge is there (or has no zone)
		bool onlyAngelIsland = true;
		for (int day = 1; day <= 28; ++day)
		{
			fake::setTime(2027, 2, day, 9, 0);
			const Data& other = get(1);
			for (const Challenge& challenge : other.mChallenges)
				onlyAngelIsland = onlyAngelIsland && (challenge.mZone == 0);
		}
		check(onlyAngelIsland, "with only Angel Island reached, all challenges are there");
		check(getRank(1) == "Rookie" && getRank(2) == "Rookie" && getRank(3) == "Ring Collector" && getRank(25) == "Hyper Hero" && getRank(26) == "Robotnik's Nightmare" &&
			  getRank(27) == "Robotnik's Nightmare II" && getRank(30) == "Robotnik's Nightmare V" && getRank(74) == "Robotnik's Nightmare XLIX",
			  "medal ranks, a new one every two levels, then numerals past the top (" + getRank(74) + ")");
		check(getMetal(4) == Metal::BRONZE && getMetal(5) == Metal::SILVER && getMetal(10) == Metal::GOLD && getMetal(15) == Metal::DIAMOND &&
			  getMetal(20) == Metal::PLATINUM && getMetal(25) == Metal::PLATINUM && getMetal(26) == Metal::EMERALD && getMetal(500) == Metal::EMERALD,
			  "medal metals: bronze 1-4, silver 5-9, gold 10-14, diamond 15-19, platinum 20-25, emerald from 26");
		check(getLevel(49) == 1 && getLevel(50) == 2 && getLevel(110) == 3 && getXpIntoLevel(120) == 10 && getXpForNextLevel(2) == 60 &&
			  getLevel(949) == 10 && getLevel(950) == 11 && getXpForNextLevel(11) == 150 && getXpForNextLevel(40) == 150 && getLevel(2300) == 20,
			  "medal levels: 50 XP for the first, 10 more for each after it, up to 150 (level 20 at 2,300 XP)");
		check(getSlotsForLevel(9) == 3 && getSlotsForLevel(10) == 4 && getSlotsForLevel(20) == 5, "an Expert challenge from level 10, Robotnik's Special from level 20");
	}

	void challengeTypes()
	{
		using namespace n3ds::challenges;
		FTX::FileSystem->createDirectory(L"sdmc:/3ds/sonic3air/savedata");
		fake::setTime(2026, 11, 23, 18, 0);
		std::ofstream("sdmc:/3ds/sonic3air/savedata/3ds_challenges.txt") << "date=20261123\nxp=0\nbeaten=0\nperfect_days=0\nzones=15\n"
			"challenge=8 0 0 10 0 0\nchallenge=9 1 0 0 0 0\nchallenge=13 3 0 0 0 0\nchallenge=11 0 0 20000 0 0\nchallenge=12 0 0 3 0 0\n";
		const Data& data = get(15);
		check(data.mCount == 5 && describe(data.mChallenges[0]) == "Destroy 10 badniks" && describe(data.mChallenges[1]) == "Earn an extra life in Hydrocity" &&
			  describe(data.mChallenges[2]) == "All 3 elemental shields in Carnival Night 1" && describe(data.mChallenges[3]) == "Score 20,000 points in one act" &&
			  describe(data.mChallenges[4]) == "Finish Angel Island as Knuckles", "new challenge types are described (" + describe(data.mChallenges[3]) + ")");

		Frame frame;
		frame.mZone = 1;
		frame.mLives = 3;
		frame.mScore = 5000;
		for (int k = 0; k < 9; ++k)
		{
			frame.mBadniksDestroyed = 1;
			onFrame(frame);
		}
		frame.mBadniksDestroyed = 0;
		check(!data.mChallenges[0].mDone && describeProgress(data.mChallenges[0]) == "9 / 10 badniks", "9 badniks: not yet");
		frame.mBadniksDestroyed = 1;
		onFrame(frame);
		frame.mBadniksDestroyed = 0;
		check(data.mChallenges[0].mDone && std::string(getTaunt(0, true, data.mDate)).find("badniks") != std::string::npos, "the 10th badnik: beaten, and Robotnik misses his badniks");

		frame.mLives = 4;
		onFrame(frame);
		check(data.mChallenges[1].mDone, "an extra life in Hydrocity");

		frame.mShields = 0x10;
		onFrame(frame);
		frame.mShields = 0x20;
		onFrame(frame);
		frame.mShields = 0x40;
		onFrame(frame);
		check(!data.mChallenges[2].mDone, "all three shields in Hydrocity don't count for Carnival Night 1");
		frame.mZone = 3;
		frame.mShields = 0x10;
		onFrame(frame);
		frame.mShields = 0x20;
		onFrame(frame);
		check(!data.mChallenges[2].mDone && describeProgress(data.mChallenges[2]) == "Best: 2 / 3 shields", "fire and lightning in Carnival Night 1: 2 of 3 shields");
		frame.mAct = 1;		// A new act starts the shields over
		frame.mShields = 0x40;
		onFrame(frame);
		frame.mAct = 0;
		frame.mShields = 0x40;
		onFrame(frame);
		check(!data.mChallenges[2].mDone, "the bubble shield after leaving the act doesn't complete the set");
		frame.mShields = 0x10;
		onFrame(frame);
		frame.mShields = 0x20;
		onFrame(frame);
		check(data.mChallenges[2].mDone, "all three in one go through Carnival Night 1: beaten");
		frame.mZone = 1;		// Back to Hydrocity: a new act, counting points from here
		frame.mShields = 0;
		onFrame(frame);

		frame.mScore = 5000 + 19990;
		onFrame(frame);
		check(!data.mChallenges[3].mDone, "19,990 points in the act: not yet");
		frame.mScore = 5000 + 20000;
		onFrame(frame);
		check(data.mChallenges[3].mDone, "20,000 points in the act: beaten");

		actDone(0, 0, 0, 100, false, false, 2);
		check(!data.mChallenges[4].mDone, "Angel Island as Tails doesn't count for Knuckles");
		actDone(0, 0, 0, 100, false, false, 4);
		check(data.mChallenges[4].mDone && data.mPerfectDays == 1, "as Knuckles (and Tails): beaten, a perfect day");
	}

	void challengeTypes2()
	{
		using namespace n3ds::challenges;
		FTX::FileSystem->createDirectory(L"sdmc:/3ds/sonic3air/savedata");
		fake::setTime(2026, 11, 23, 18, 0);
		std::ofstream("sdmc:/3ds/sonic3air/savedata/3ds_challenges.txt") << "date=20261123\nxp=0\nbeaten=0\nperfect_days=0\nzones=15\n"
			"challenge=10 0 0 0 0 0\nchallenge=14 1 0 0 0 0\nchallenge=12 1 0 2 0 0\n";
		const Data& data = get(15);
		check(describe(data.mChallenges[1]) == "Clear Hydrocity, never low on air", "Hydrocity's challenge is described");
		Frame frame;
		frame.mZone = 0x14;
		onFrame(frame);
		check(data.mChallenges[0].mDone, "the Glowing Spheres bonus stage: beaten");
		actDone(1, 0, 0, 100, false, true);
		check(!data.mChallenges[1].mDone, "low on air in Hydrocity: not beaten");
		actDone(1, 1, 0, 100, true, false);
		check(data.mChallenges[1].mDone, "Hydrocity without running low on air (a hit is fine): beaten");
		actDone(1, 0, 0, 100, false, false, 2);
		check(data.mChallenges[2].mDone, "Hydrocity as Tails: beaten");
	}

	void challengeTypes3()
	{
		using namespace n3ds::challenges;
		FTX::FileSystem->createDirectory(L"sdmc:/3ds/sonic3air/savedata");
		fake::setTime(2026, 11, 24, 18, 0);
		std::ofstream("sdmc:/3ds/sonic3air/savedata/3ds_challenges.txt") << "date=20261124\nxp=0\nbeaten=0\nperfect_days=0\nzones=4095\n"
			"challenge=15 7 0 0 0 0\nchallenge=16 0 0 5 0 0\nchallenge=17 0 0 30005 0 0\nchallenge=19 3 0 10 0 0\nchallenge=20 8 1 0 0 0\n";
		const Data& data = get(4095);
		check(data.mCount == 5, "five challenges read back");
		check(describe(data.mChallenges[0]) == "Outrun your shadow in Mushroom Hill 1", "the shadow is described: " + describe(data.mChallenges[0]));
		check(describe(data.mChallenges[2]) == "Use the insta-shield 5 times", "a move is described: " + describe(data.mChallenges[2]));
		check(describe(data.mChallenges[3]) == "Pop 10 balloons in Carnival Night" && describe(data.mChallenges[4]) == "Clear Sandopolis 2 without a ghost", "balloons and ghosts are described");

		bool deadly = true;
		check(isShadowAct(7, 0, deadly) && !deadly && !isShadowAct(7, 1, deadly) && !isShadowAct(0, 1, deadly), "the shadow runs in Mushroom Hill 1 only, not deadly");

		n3ds::challenges::ActResult result;
		result.mZone = 7;
		result.mAct = 0;
		result.mShadowTouched = true;
		onActCompleted(result);
		check(!data.mChallenges[0].mDone, "touched by the shadow: not beaten");
		result.mShadowTouched = false;
		onActCompleted(result);
		check(data.mChallenges[0].mDone && !isShadowAct(7, 0, deadly), "outran the shadow: beaten, and he's gone");

		Frame frame;
		frame.mZone = 0;
		frame.mMonitorsBroken = 2;
		onFrame(frame);
		frame.mMonitorsBroken = 0;
		frame.mMovesUsed[(int)Move::INSTA_SHIELD] = 1;
		frame.mMovesUsed[(int)Move::SPINDASH] = 1;
		for (int k = 0; k < 4; ++k)
			onFrame(frame);
		check(data.mChallenges[1].mProgress == 2 && !data.mChallenges[1].mDone, "two monitors counted");
		check(data.mChallenges[2].mProgress == 4 && !data.mChallenges[2].mDone && describeProgress(data.mChallenges[2]) == "4 / 5", "four insta-shields counted (spindashes don't)");
		onFrame(frame);
		check(data.mChallenges[2].mDone, "five insta-shields: beaten");
		frame = Frame();
		frame.mMonitorsBroken = 3;
		onFrame(frame);
		check(data.mChallenges[1].mDone, "five monitors: beaten");

		frame = Frame();
		frame.mZone = 1;
		frame.mBalloonsPopped = 4;
		onFrame(frame);
		check(data.mChallenges[3].mProgress == 0, "balloons only count in Carnival Night");
		frame.mZone = 3;
		for (int k = 0; k < 3; ++k)
			onFrame(frame);
		check(data.mChallenges[3].mDone, "12 balloons popped in Carnival Night: beaten");

		result = n3ds::challenges::ActResult();
		result.mZone = 8;
		result.mAct = 1;
		result.mGhostsWoken = true;
		onActCompleted(result);
		check(!data.mChallenges[4].mDone, "a ghost came out: not beaten");
		result.mGhostsWoken = false;
		onActCompleted(result);
		check(data.mChallenges[4].mDone, "Sandopolis 2 without a ghost (saved before): still beaten");

		check(getAmbushChance() == 1, "the shadow ambush is on at 1% by default");
		setAmbushChance(25);
		addAmbushSurvived();
		n3ds::filewriter::waitUntilDone();
		const std::string saved = readFile("sdmc:/3ds/sonic3air/savedata/3ds_challenges.txt");
		check(saved.find("ambush=25\n") != std::string::npos && saved.find("ambushes_survived=1\n") != std::string::npos, "its chance and the ambushes outrun are saved");
	}

	void challengeLights()
	{
		using namespace n3ds::challenges;
		FTX::FileSystem->createDirectory(L"sdmc:/3ds/sonic3air/savedata");
		fake::setTime(2026, 11, 24, 18, 0);
		std::ofstream("sdmc:/3ds/sonic3air/savedata/3ds_challenges.txt") << "date=20261124\nxp=0\nbeaten=0\nperfect_days=0\nzones=16777215\n"
			"challenge=21 8 1 3 0 0\nchallenge=8 0 0 10 0 0\nchallenge=6 0 0 0 0 0\n";
		const Data& data = get(0xffffff);
		check(describe(data.mChallenges[0]) == "Turn the lights on 3 times in Sandopolis 2", "the light switches are described: " + describe(data.mChallenges[0]));
		Frame frame;
		frame.mZone = 8;
		frame.mAct = 1;
		frame.mLightsTurnedOn = 1;
		onFrame(frame);
		onFrame(frame);
		check(data.mChallenges[0].mProgress == 2 && !data.mChallenges[0].mDone && describeProgress(data.mChallenges[0]) == "2 / 3 times", "the lights turned on twice");
		onFrame(frame);
		check(data.mChallenges[0].mDone, "three times: beaten");
	}

	uint32_t onlySpindash()  { return 1u << (int)n3ds::challenges::Move::SPINDASH; }

	void challengeGeneration()
	{
		// Many days: no move that can't be used, no "Go Super" without the emeralds, no act challenge in Angel Island 1,
		// and a deadly shadow only as Robotnik's Special
		using namespace n3ds::challenges;
		FTX::FileSystem->createDirectory(L"sdmc:/3ds/sonic3air/savedata");
		fake::setTime(2026, 1, 1, 12, 0);
		std::ofstream("sdmc:/3ds/sonic3air/savedata/3ds_challenges.txt") << "date=20250101\nxp=2300\nbeaten=60\nperfect_days=12\nzones=16777215\n";
		setAvailableMovesProvider(&onlySpindash);
		int counts[(int)Type::_NUM] = { 0 };
		bool badMove = false, goSuper = false, angelIsland1 = false, deadlyOutsideSpecial = false, normalShadowInSpecial = false;
		for (int day = 0; day < 400; ++day)
		{
			const int month = 1 + (day / 28) % 12;
			const int year = 2026 + day / (28 * 12);
			fake::setTime(year, month, 1 + day % 28, 12, 0);
			const Data& data = get(0xffffff);
			for (int k = 0; k < data.mCount; ++k)
			{
				const Challenge& challenge = data.mChallenges[k];
				++counts[(int)challenge.mType];
				if (challenge.mType == Type::MOVES && challenge.mTarget / MOVE_FACTOR != 0)
					badMove = true;
				if (challenge.mType == Type::GO_SUPER)
					goSuper = true;
				const bool actSpecific = (challenge.mType == Type::ACT_UNDER_TIME || challenge.mType == Type::FAST_NO_HIT || challenge.mType == Type::SHADOW_RUN);
				if (actSpecific && challenge.mZone == 0 && challenge.mAct == 0)
					angelIsland1 = true;
				if (challenge.mType == Type::SHADOW_RUN && challenge.mTarget == 1 && k != 4)
					deadlyOutsideSpecial = true;
				if (challenge.mType == Type::SHADOW_RUN && challenge.mTarget != 1 && k == 4)
					normalShadowInSpecial = true;
			}
		}
		check(!badMove, "only the spindash for move challenges, when nothing else can be used");
		check(!goSuper, "no Go Super without the emeralds");
		check(!angelIsland1, "no act challenge in Angel Island 1");
		check(!deadlyOutsideSpecial && !normalShadowInSpecial, "the deadly shadow only as Robotnik's Special");
		check(counts[(int)Type::SHADOW_RUN] > 0 && counts[(int)Type::MONITORS] > 0 && counts[(int)Type::MOVES] > 0 && counts[(int)Type::BALLOONS] > 0 && counts[(int)Type::LIGHT_SWITCHES] > 0 && counts[(int)Type::NO_GHOSTS] == 0,
			  "the new challenges come up: shadow " + std::to_string(counts[(int)Type::SHADOW_RUN]) + ", monitors " + std::to_string(counts[(int)Type::MONITORS]) +
			  ", moves " + std::to_string(counts[(int)Type::MOVES]) + ", balloons " + std::to_string(counts[(int)Type::BALLOONS]) + ", light switches " + std::to_string(counts[(int)Type::LIGHT_SWITCHES]) + ", no ghost challenges any more");

		// With all the emeralds seen once, "Go Super" can come up
		Frame frame;
		frame.mEmeralds = 7;
		onFrame(frame);
		for (int day = 0; day < 200 && !goSuper; ++day)
		{
			fake::setTime(2028, 1 + (day / 28) % 12, 1 + day % 28, 12, 0);
			const Data& data = get(0xffffff);
			for (int k = 0; k < data.mCount; ++k)
				goSuper = goSuper || (data.mChallenges[k].mType == Type::GO_SUPER);
		}
		check(goSuper, "Go Super comes up once the emeralds were seen");
	}

	void editedFiles()
	{
		// Players may edit their files: silly numbers load right away and don't overflow
		fake::setTime(2026, 11, 24, 10, 0);
		writeJournal("started=20261001\nlast_day=20261124\ndays_played=28\nstamps=99\ncards_completed=4000000000\nrewarded_cards=0\nvouchers=4294967000\nbonus_boxes=0\n");
		const auto start = std::chrono::steady_clock::now();
		const n3ds::journal::Data& journal = n3ds::journal::get();
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		check(seconds < 1.0, "a journal with 4 billion cards loads right away (" + std::to_string(seconds) + " s)");
		check(journal.mVouchers == 0xffffffffu && journal.mBonusBoxes == 4000000000u && journal.mStamps == n3ds::journal::STAMPS_PER_CARD, "vouchers stop at the biggest number, stamps at a full card");
		check((int)journal.mPostcards.size() == n3ds::journal::getNumPossiblePostcards(), "and all postcards are there, no more");

		using namespace n3ds::challenges;
		check(getLevel(4000000000u) > 26000000 && getXpIntoLevel(4000000000u) < getXpForNextLevel(getLevel(4000000000u)), "4 billion XP: medal level " + std::to_string(getLevel(4000000000u)));
		bool same = true;
		for (uint32_t xp = 0; xp < 20000; xp += 7)
		{
			int level = 1;
			uint32_t rest = xp;
			while (rest >= getXpForNextLevel(level))
			{
				rest -= getXpForNextLevel(level);
				++level;
			}
			same = same && (getLevel(xp) == level) && (getXpIntoLevel(xp) == rest);
		}
		check(same, "the quick medal level is the same as counting level by level");
		check(getRank(getLevel(4000000000u)).length() < 60, "and its rank name stays short: " + getRank(getLevel(4000000000u)));
	}

	void challengesSlots()
	{
		using namespace n3ds::challenges;
		FTX::FileSystem->createDirectory(L"sdmc:/3ds/sonic3air/savedata");

		// Yesterday's challenges at medal level 20: today has all five
		fake::setTime(2026, 11, 23, 18, 0);
		std::ofstream("sdmc:/3ds/sonic3air/savedata/3ds_challenges.txt") << "date=20261122\nxp=2300\nbeaten=60\nperfect_days=12\nzones=4095\n"
			"challenge=0 1 0 50 50 1\nchallenge=4 0 0 150 0 0\nchallenge=6 1 0 0 0 0\nchallenge=3 0 0 250 0 0\n";
		const Data& data = get(4095);
		std::string list;
		for (int k = 0; k < data.mCount; ++k)
			list += ((k > 0) ? " / " : "") + describe(data.mChallenges[k]);
		check(data.mDate == 20261123 && data.mCount == 5, "at medal level 20, five challenges a day: " + list);
		check(getXpFor(3) == 40 && getXpFor(4) == 50 && std::string(getSlotName(4)) == "Robotnik's Special", "expert 40 XP, Robotnik's Special 50 XP");
	}

	void postcardsAll()
	{
		// Loads of cards: every zone first, then the rest, and never more than there are pictures
		fake::setTime(2026, 11, 21, 21, 0);
		writeJournal("started=20250101\nlast_day=20261121\ndays_played=420\nstamps=0\ncards_completed=80\npostcard=zone05_act2b 20250108\npostcard=funny_decoys 20250115\n");
		const n3ds::journal::Data& journal = n3ds::journal::get();
		check(n3ds::journal::getNumPossiblePostcards() == 71 && n3ds::journal::getNumFunnyPostcards() == 21, "71 postcards to collect: 50 zone pictures and 21 funny ones");
		check((int)journal.mPostcards.size() == 71, "80 cards: all 71 postcards, no more (" + std::to_string(journal.mPostcards.size()) + ")");
		check(journal.mPostcards[0].getSpriteName() == "zone05_act2b" && journal.mPostcards[0].mDate == 20250108, "a saved postcard is read back (IceCap Act 2, 8 Jan 2025)");
		check(journal.mPostcards[1].getSpriteName() == "funny_decoys" && journal.mPostcards[1].mDate == 20250115, "... and a funny one (decoys, 15 Jan 2025)");
		std::vector<int> zoneOrder;
		for (const n3ds::journal::Postcard& postcard : journal.mPostcards)
			if (postcard.mFunny < 0)
				zoneOrder.push_back(postcard.mZone);
		bool firstAllZones = (zoneOrder.size() >= 14);
		for (int zone = 1; zone <= 14 && firstAllZones; ++zone)
			firstAllZones = (std::find(zoneOrder.begin(), zoneOrder.begin() + 14, zone) != zoneOrder.begin() + 14);
		check(firstAllZones, "the first 14 zone postcards are one from each zone");
		bool unique = true;
		for (size_t i = 0; i < journal.mPostcards.size(); ++i)
			for (size_t k = 0; k < i; ++k)
				unique = unique && (journal.mPostcards[i].getSpriteName() != journal.mPostcards[k].getSpriteName());
		check(unique, "no postcard twice");

		frames(60);
		shot("badges_60_cards_24h");
	}
}

// Mods over Wi-Fi: the real little web server on this PC, a mod uploaded with curl like a browser does
void modServer()
{
	using namespace n3ds;
	const auto run = [](const std::string& command) -> std::string
	{
		std::string output;
		if (FILE* pipe = popen(command.c_str(), "r"))
		{
			char buffer[4096];
			size_t count;
			while ((count = fread(buffer, 1, sizeof(buffer), pipe)) > 0)
				output.append(buffer, count);
			pclose(pipe);
		}
		return output;
	};
	const auto exists = [](const std::string& path) { struct stat info; return stat(path.c_str(), &info) == 0; };

	// The checks for what's in a zip
	{
		std::string root, error;
		check(modserver::findModRoot({ "mod.json", "sprites/a.png" }, root, error) && root.empty(), "mod.json at the top: the zip is the mod");
		bool found = modserver::findModRoot({ "Cool Mod/", "Cool Mod/mod.json", "Cool Mod/sprites/a.png", "__MACOSX/Cool Mod/mod.json" }, root, error);
		check(found && root == "Cool Mod/", "mod.json in an extra folder: that folder is the mod ('" + root + "')");
		found = modserver::findModRoot({ "A/mod.json", "B/mod.json" }, root, error);
		check(!found, "two mods in one zip: refused (" + error + ")");
		found = modserver::findModRoot({ "readme.txt" }, root, error);
		check(!found, "no mod.json: refused (" + error + ")");
		check(!modserver::isSafeZipPath("../evil.txt") && !modserver::isSafeZipPath("a/../../b") && !modserver::isSafeZipPath("/abs") &&
			  !modserver::isSafeZipPath("sdmc:/x") && modserver::isSafeZipPath("a/b..c/d.png"), "unsafe paths in zips are refused");
		check(modserver::cleanFolderName("My Mod: v2?") == "My Mod_ v2_" && modserver::cleanFolderName(" .. ") == "mod", "folder names are cleaned up");
	}

	// A QR code for the address is made on the 3DS
	{
		const qrcode::Code code = qrcode::encode("http://192.168.178.200:8080/");
		check(code.mSize == 29, "QR code for the address: " + std::to_string(code.mSize) + " modules (version 3)");
		std::ofstream out(gOut + "/../qr_test.txt");
		out << "http://192.168.178.200:8080/\n";
		for (int y = 0; y < code.mSize; ++y)
		{
			for (int x = 0; x < code.mSize; ++x)
				out << (code.get(x, y) ? '#' : '.');
			out << "\n";
		}
	}

	// A .7z made right here (with the 7z tool, if it's there), and a file that's no archive at all
	if (run("command -v 7z").find("7z") != std::string::npos)
	{
		mkdir("sdmc:/3ds", 0777);
		mkdir("sdmc:/3ds/sonic3air", 0777);
		const std::string folder = "sevenzip_src/Seven Mod/";
		run("mkdir -p '" + folder + "sprites' && printf '{\\\"Metadata\\\":{\\\"Name\\\":\\\"Seven Mod\\\"}}' > '" + folder + "mod.json' && head -c 70000 /dev/urandom > '" + folder + "sprites/big.bin'");
		run("cd sevenzip_src && 7z a -bd -y ../seven.7z 'Seven Mod' >/dev/null");
		const modserver::InstallResult result = modserver::installZip("seven.7z", "seven.7z", "sdmc:/3ds/sonic3air/mods7z/", "sdmc:/3ds/sonic3air/seven_install/", nullptr);
		check(result.mSuccess && result.mFolder == "Seven Mod" && exists("sdmc:/3ds/sonic3air/mods7z/Seven Mod/sprites/big.bin"), "a .7z mod unpacks: " + result.mMessage);
		run("head -c 1000 /dev/urandom > notanarchive.zip");
		const modserver::InstallResult bad = modserver::installZip("notanarchive.zip", "notanarchive.zip", "sdmc:/3ds/sonic3air/mods7z/", "sdmc:/3ds/sonic3air/seven_install/", nullptr);
		check(!bad.mSuccess && bad.mMessage.find(".rar or .7z") != std::string::npos, "a file that's no archive: " + bad.mMessage);
	}

	// The server
	mkdir("sdmc:/3ds", 0777);
	const std::string modsDir = "sdmc:/3ds/sonic3air/savedata/mods/";
	const std::string workDir = "sdmc:/3ds/sonic3air/savedata/";
	modserver::setInstalledModsJson("[{\"name\":\"Old Mod\",\"folder\":\"Old Mod\",\"active\":true}]");
	check(modserver::start(modsDir, workDir, inet_addr("127.0.0.1")), "the server starts");
	const modserver::Status ready = modserver::getStatus();
	check(ready.mState == modserver::Status::State::READY, "... and waits for an upload at " + ready.mAddress);
	const std::string base = ready.mAddress;

	const std::string page = run("curl -s " + base);
	check(page.find("<title>Sonic 3 A.I.R. 3DS - Mods</title>") != std::string::npos && page.find("/upload") != std::string::npos, "the page is served");
	check(run("curl -s " + base + "mods").find("Old Mod") != std::string::npos, "the list of mods is served");
	check(run("curl -s -o /dev/null -w '%{http_code}' " + base + "nothing") == "404", "unknown addresses: 404");

	// A mod in an extra folder, with a shader and a big image, as most downloads are
	run("python3 -c \"import zipfile, struct; z = zipfile.ZipFile('upload_test.zip', 'w', zipfile.ZIP_DEFLATED); "
		"z.writestr('Super Mod/mod.json', '{\\\"Metadata\\\": {\\\"Name\\\": \\\"Super Test Mod\\\", \\\"Author\\\": \\\"Me\\\"}}'); "
		"z.writestr('Super Mod/sprites/big.png', b'\\x89PNG\\r\\n\\x1a\\n' + b'\\0\\0\\0\\x0dIHDR' + struct.pack('>II', 2048, 512) + bytes(200)); "
		"z.writestr('Super Mod/shaders/glow.glsl', 'void main() {}'); "
		"z.writestr('Super Mod/data/' + 'x' * 10 + '.bin', bytes(300000)); z.close()\"");
	const std::string uploadAnswer = run("curl -s -X POST --data-binary @upload_test.zip -H 'X-File-Name: Super%20Mod%20v1.zip' " + base + "upload");
	check(uploadAnswer.find("\"installs\":0") != std::string::npos, "upload accepted (" + uploadAnswer + ")");

	modserver::Status status;
	std::string installed;
	for (int k = 0; k < 400; ++k)
	{
		n3ds::filewriter::update();
		status = modserver::getStatus();
		if (status.mInstalls > 0)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	check(status.mInstalls == 1 && status.mLastSuccess, "unpacked: " + status.mLastMessage);
	const bool told = modserver::takeInstalledMod(installed);
	check(told && installed == "Super Mod", "the game gets told the new mod's folder ('" + installed + "')");
	check(exists(modsDir + "Super Mod/mod.json") && exists(modsDir + "Super Mod/sprites/big.png") && exists(modsDir + "Super Mod/data/xxxxxxxxxx.bin"),
		  "its files are in the mods folder, without the extra folder");
	check(!exists(workDir + "wifi_upload.bin") && !exists(workDir + "wifi_install"), "nothing left over");
	check(status.mLastWarnings.size() == 2, "warnings for the shader and the big image (" + std::to_string(status.mLastWarnings.size()) + ")");
	check(status.mLastMessage.find("Super Test Mod") != std::string::npos, "the message names the mod by its name in mod.json");
	const std::string statusJson = run("curl -s " + base + "status");
	check(statusJson.find("\"installs\":1") != std::string::npos && statusJson.find("\"success\":true") != std::string::npos, "the page sees it too");

	// Again: an update replaces it
	run("python3 -c \"import zipfile; z = zipfile.ZipFile('upload_test2.zip', 'w'); z.writestr('mod.json', '{}'); z.writestr('new.txt', 'new'); z.close()\"");
	run("curl -s -X POST --data-binary @upload_test2.zip -H 'X-File-Name: Super%20Mod.zip' " + base + "upload");
	for (int k = 0; k < 400 && modserver::getStatus().mInstalls < 2; ++k)
	{
		n3ds::filewriter::update();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	status = modserver::getStatus();
	check(status.mInstalls == 2 && exists(modsDir + "Super Mod/new.txt") && !exists(modsDir + "Super Mod/sprites"), "uploading it again replaces the old version (" + status.mLastMessage + ")");

	// Not a mod
	run("python3 -c \"import zipfile; z = zipfile.ZipFile('upload_test3.zip', 'w'); z.writestr('readme.txt', 'hi'); z.close()\"");
	run("curl -s -X POST --data-binary @upload_test3.zip -H 'X-File-Name: notamod.zip' " + base + "upload");
	for (int k = 0; k < 400; ++k)
	{
		n3ds::filewriter::update();
		status = modserver::getStatus();
		if (status.mState == modserver::Status::State::READY && !status.mLastSuccess)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	check(!status.mLastSuccess && status.mLastMessage.find("No mod.json") != std::string::npos, "a zip without a mod: refused (" + status.mLastMessage + ")");
	check(run("curl -s -X POST --data-binary @upload_test3.zip -H 'X-File-Name: picture.png' " + base + "upload").find("Only .zip") != std::string::npos, "only .zip files");

	modserver::stop();
	check(!modserver::isRunning() && run("curl -s -m 2 " + base) .empty(), "stopped: the page is gone");
}

// The GameBanana mod browser: GameBanana's real answers (saved in "fixtures"), and with N3DS_TEST_ONLINE=1 the real thing
void gameBanana()
{
	using namespace n3ds;
	const std::string fixtures = std::string(HOSTTEST_DIR) + "/fixtures/";
	const auto readFile = [](const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	};
	const auto exists = [](const std::string& path) { struct stat info; return stat(path.c_str(), &info) == 0; };

	// Addresses
	check(gamebanana::getListUrl(gamebanana::Listing::POPULAR, "", 2) == "https://gamebanana.com/apiv11/Mod/Index?_nPage=2&_nPerpage=15&_aFilters%5BGeneric_Game%5D=6878&_sSort=Generic_MostDownloaded",
		  "list address: " + gamebanana::getListUrl(gamebanana::Listing::POPULAR, "", 2));
	const std::string searchUrl = gamebanana::getListUrl(gamebanana::Listing::SEARCH, "super sonic & tails", 1);
	check(searchUrl.find("_sSearchString=super%20sonic%20%26%20tails&") != std::string::npos, "search address with the words encoded: " + searchUrl);

	// Lists
	{
		std::vector<gamebanana::ModSummary> mods;
		int total = 0;
		std::string error;
		const bool parsed = gamebanana::parseModList(readFile(fixtures + "gb_list_popular.json"), mods, total, error);
		check(parsed && mods.size() == 15 && total > 6000, "most downloaded: " + std::to_string(mods.size()) + " mods of " + std::to_string(total));
		if (!mods.empty())
		{
			const gamebanana::ModSummary& first = mods[0];
			check(first.mId != 0 && !first.mName.empty() && !first.mAuthor.empty() && first.mThumbnailUrl.find("https://images.gamebanana.com/") == 0 && first.mThumbnailUrl.find("/100-90_") != std::string::npos,
				  "first: \"" + first.mName + "\" by " + first.mAuthor + " (" + first.mCategory + "), " + std::to_string(first.mLikes) + " likes, picture " + first.mThumbnailUrl);
		}
		mods.clear();
		const bool searched = gamebanana::parseModList(readFile(fixtures + "gb_search_tails.json"), mods, total, error);
		bool allTails = searched && !mods.empty();
		std::string names;
		for (const gamebanana::ModSummary& mod : mods)
			names += (names.empty() ? "" : ", ") + mod.mName;
		check(allTails && total > 100, "search for \"tails\": " + std::to_string(mods.size()) + " of " + std::to_string(total) + " (" + names.substr(0, 120) + "...)");
		const bool htmlRefused = !gamebanana::parseModList("<html>Bad gateway</html>", mods, total, error);
		check(htmlRefused && !error.empty(), "an HTML error page instead of JSON: '" + error + "'");
		const bool errorRefused = !gamebanana::parseModList("{\"_sErrorCode\":\"Throttled\",\"_sErrorMessage\":\"Too many requests\"}", mods, total, error);
		check(errorRefused && error.find("Too many requests") != std::string::npos, "GameBanana's own error message is shown: '" + error + "'");
	}

	// Details
	{
		gamebanana::ModDetails details;
		std::string error;
		const bool parsedDetails = gamebanana::parseModDetails(readFile(fixtures + "gb_details_zip.json"), details, error);
		check(parsedDetails && details.mName == "Character Underwater Palette Scripted" && details.mFiles.size() == 2,
			  "details: \"" + details.mName + "\" by " + details.mAuthor + ", " + std::to_string(details.mFiles.size()) + " files, " + std::to_string(details.mDownloads) + " downloads");
		if (details.mFiles.size() == 2)
		{
			check(details.mFiles[0].mName == "character_underwater_palette_scripted_v11.zip" && details.mFiles[0].mDateAdded >= details.mFiles[1].mDateAdded,
				  "newest file first: " + details.mFiles[0].mName + " (" + std::to_string(details.mFiles[0].mSize) + " bytes)");
			check(gamebanana::getFileProblem(details.mFiles[0]) == nullptr && !details.mFiles[0].mFlagged, "a .zip that passed GameBanana's checks can be installed");
		}
		check(details.mText.find('<') == std::string::npos && details.mText.find("&nbsp;") == std::string::npos, "description as plain text: \"" + details.mText.substr(0, 80) + "...\"");
		check(!details.mImageUrls.empty() && details.mImageUrls[0].find("/220-90_") != std::string::npos, "preview pictures: " + std::to_string(details.mImageUrls.size()));

		gamebanana::ModDetails rar;
		const bool parsedRar = gamebanana::parseModDetails(readFile(fixtures + "gb_details_rar.json"), rar, error);
		check(parsedRar && !rar.mFiles.empty(), "a mod with .rar files: " + std::to_string(rar.mFiles.size()) + " files");
		const char* problem = rar.mFiles.empty() ? nullptr : gamebanana::getFileProblem(rar.mFiles[0]);
		check(nullptr == problem, std::string("... can be installed on the 3DS now") + (problem ? std::string(": no, ") + problem : std::string()));
		gamebanana::FileInfo exe;
		exe.mName = "tool.exe";
		problem = gamebanana::getFileProblem(exe);
		check(nullptr != problem, std::string("a file that's no archive can't: ") + (problem ? problem : "(no reason)"));
	}

	// Text
	check(gamebanana::htmlToText("Hello<br>World &amp; more<br><br><br>&nbsp;<ul><li>One</li><li>Two</li></ul>") == "Hello\nWorld & more\n\n- One\n- Two",
		  "HTML to text: '" + gamebanana::htmlToText("Hello<br>World &amp; more<br><br><br>&nbsp;<ul><li>One</li><li>Two</li></ul>") + "'");
	check(gamebanana::toDisplayText("Sonic\xe2\x80\x99s \xe2\x80\x9c" "caf\xc3\xa9\xe2\x80\x9d \xe2\x80\x93 ok\xe2\x80\xa6 \xf0\x9f\x94\xa5!") == "Sonic's \"cafe\" - ok... !",
		  "display text: '" + gamebanana::toDisplayText("Sonic\xe2\x80\x99s \xe2\x80\x9c" "caf\xc3\xa9\xe2\x80\x9d \xe2\x80\x93 ok\xe2\x80\xa6 \xf0\x9f\x94\xa5!") + "' (emoji left out)");
	check(gamebanana::toDisplayText("cut \xe2\x80") == "cut ", "a character cut off at the end doesn't break anything");

	// Pictures
	{
		gamebanana::Thumbnail thumbnail;
		const bool decoded = gamebanana::decodeJpeg(readFile(fixtures + "gb_thumb_100.jpg"), 100, thumbnail);
		check(decoded && thumbnail.mWidth == 100 && thumbnail.mHeight == 56 && thumbnail.mPixels.size() == 5600,
			  "thumbnail JPEG decoded: " + std::to_string(thumbnail.mWidth) + "x" + std::to_string(thumbnail.mHeight));
		gamebanana::Thumbnail small;
		const bool decodedSmall = gamebanana::decodeJpeg(readFile(fixtures + "gb_thumb_100.jpg"), 50, small);
		check(decodedSmall && small.mWidth == 50 && small.mHeight == 28, "... and scaled down while decoding: " + std::to_string(small.mWidth) + "x" + std::to_string(small.mHeight));
		gamebanana::Thumbnail webp;
		check(!gamebanana::decodeJpeg("RIFF....WEBPVP8 ", 100, webp), "a WebP picture is skipped (shown as a placeholder)");
		if (thumbnail.mWidth == 100)
		{
			// As a picture, to look at
			std::ofstream out(gOut + "/gamebanana_thumbnail.ppm", std::ios::binary);
			out << "P6\n100 56\n255\n";
			for (const uint32_t pixel : thumbnail.mPixels)
				out << (char)(pixel & 0xff) << (char)((pixel >> 8) & 0xff) << (char)((pixel >> 16) & 0xff);
		}
	}

	if (nullptr == getenv("N3DS_TEST_ONLINE"))
	{
		printf("  (set N3DS_TEST_ONLINE=1 to also test against the real GameBanana)\n");
		return;
	}

	// The real GameBanana
	if (const char* ca = getenv("CURL_CA_BUNDLE"))
		setenv("N3DS_TEST_CA_FILE", ca, 1);
	mkdir("sdmc:/3ds", 0777);
	mkdir("sdmc:/3ds/sonic3air", 0777);
	mkdir("sdmc:/3ds/sonic3air/savedata", 0777);
	const std::string modsDir = "sdmc:/3ds/sonic3air/savedata/mods/";
	const std::string workDir = "sdmc:/3ds/sonic3air/savedata/";
	gamebanana::start(modsDir, workDir);

	const auto waitFor = [](const std::function<bool(const gamebanana::State&)>& done, double seconds)
	{
		const auto start = std::chrono::steady_clock::now();
		gamebanana::State state;
		while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < seconds)
		{
			state = gamebanana::getState();
			if (done(state))
				return std::make_pair(true, state);
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		return std::make_pair(false, state);
	};

	auto t0 = std::chrono::steady_clock::now();
	const auto seconds = [&]() { return std::to_string((int)(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 1000)) + " ms"; };
	gamebanana::requestList(gamebanana::Listing::NEWEST, "", 1);
	auto listed = waitFor([](const gamebanana::State& s) { return s.mListLoad == gamebanana::Load::READY || s.mListLoad == gamebanana::Load::FAILED; }, 30);
	check(listed.second.mListLoad == gamebanana::Load::READY && listed.second.mMods.size() == 15,
		  "online, newest: " + std::to_string(listed.second.mMods.size()) + " mods in " + seconds() + (listed.second.mListError.empty() ? "" : " - " + listed.second.mListError) +
		  (listed.second.mMods.empty() ? "" : ", first \"" + listed.second.mMods[0].mName + "\""));

	// Requests replace each other: only the last list counts
	t0 = std::chrono::steady_clock::now();
	gamebanana::requestList(gamebanana::Listing::POPULAR, "", 1);
	gamebanana::requestList(gamebanana::Listing::SEARCH, "underwater palette", 1);
	listed = waitFor([](const gamebanana::State& s) { return s.mListLoad == gamebanana::Load::READY || s.mListLoad == gamebanana::Load::FAILED; }, 30);
	uint32_t underwaterId = 0;
	for (const gamebanana::ModSummary& mod : listed.second.mMods)
	{
		if (mod.mName == "Character Underwater Palette Scripted")
			underwaterId = mod.mId;
	}
	check(listed.second.mListing == gamebanana::Listing::SEARCH && underwaterId == 743095, "online search \"underwater palette\" finds the mod (" + seconds() + ")");

	if (!listed.second.mMods.empty())
	{
		gamebanana::requestThumbnail(listed.second.mMods[0].mThumbnailUrl, 64);
		gamebanana::Thumbnail thumbnail;
		bool got = false;
		for (int k = 0; k < 300 && !got; ++k)
		{
			got = gamebanana::takeThumbnail(thumbnail);
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		check(got && !thumbnail.mFailed && thumbnail.mWidth >= 50, "online thumbnail: " + std::to_string(thumbnail.mWidth) + "x" + std::to_string(thumbnail.mHeight));
	}

	gamebanana::requestDetails(743095);
	auto detailed = waitFor([](const gamebanana::State& s) { return s.mDetailsLoad == gamebanana::Load::READY || s.mDetailsLoad == gamebanana::Load::FAILED; }, 30);
	check(detailed.second.mDetailsLoad == gamebanana::Load::READY && !detailed.second.mDetails.mFiles.empty(), "online details: " + std::to_string(detailed.second.mDetails.mFiles.size()) + " files");

	if (!detailed.second.mDetails.mFiles.empty())
	{
		t0 = std::chrono::steady_clock::now();
		gamebanana::requestInstall(detailed.second.mDetails.mFiles[0], detailed.second.mDetails.mName);
		auto installed = waitFor([](const gamebanana::State& s) { return s.mInstall == gamebanana::Install::DONE; }, 60);
		check(installed.second.mInstallSuccess, "online install: " + installed.second.mInstallMessage + " (" + seconds() + ")");
		std::string folder;
		const bool told = gamebanana::takeInstalledMod(folder);
		check(told && exists(modsDir + folder + "/mod.json") && exists(modsDir + folder + "/scripts/main.lemon"), "... in the mods folder: " + folder);
		check(!exists(workDir + "gamebanana_download.bin") && !exists(workDir + "gamebanana_install"), "... and the download is cleaned up");
		gamebanana::acknowledgeInstall();
	}

	// A .rar (RAR5) and a .7z mod, unpacked with libarchive
	for (const auto& test : { std::make_pair(std::string("shadow_the_hedgehog.rar"), std::string("https://gamebanana.com/dl/434763")),
							  std::make_pair(std::string("custom_super_knuckles_sprites.7z"), std::string("https://gamebanana.com/dl/483644")) })
	{
		gamebanana::FileInfo file;
		file.mName = test.first;
		file.mUrl = test.second;
		t0 = std::chrono::steady_clock::now();
		gamebanana::requestInstall(file, test.first);
		auto installed = waitFor([](const gamebanana::State& s) { return s.mInstall == gamebanana::Install::DONE; }, 60);
		std::string folder;
		const bool told = installed.second.mInstallSuccess && gamebanana::takeInstalledMod(folder);
		check(told && exists(modsDir + folder + "/mod.json"), "online " + test.first + ": " + installed.second.mInstallMessage + " -> " + folder + " (" + seconds() + ")");
		gamebanana::acknowledgeInstall();
	}

	// Cancelling a bigger download (Amy Galore, about 10 MB)
	{
		gamebanana::FileInfo big;
		big.mName = "amy_galore.zip";
		big.mUrl = "https://gamebanana.com/dl/1752484";
		big.mSize = 9607788;
		gamebanana::requestInstall(big, "Amy Galore");
		waitFor([](const gamebanana::State& s) { return s.mBytes > 0; }, 20);
		gamebanana::cancelInstall();
		auto cancelled = waitFor([](const gamebanana::State& s) { return s.mInstall == gamebanana::Install::DONE; }, 20);
		check(!cancelled.second.mInstallSuccess && cancelled.second.mInstallMessage == "Download cancelled." && !exists(workDir + "gamebanana_download.bin"),
			  "online cancel: '" + cancelled.second.mInstallMessage + "', nothing left behind");
		gamebanana::acknowledgeInstall();
	}
	gamebanana::stop();
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
		{ "filewriter", fileWriter }, { "shopreload", shopReload }, { "postcards", postcards }, { "postcardsall", postcardsAll }, { "rewards", rewards }, { "rewardsreload", rewardsReload }, { "challenges", challenges }, { "challengeslots", challengesSlots }, { "challengetypes", challengeTypes }, { "challengetypes2", challengeTypes2 }, { "challengetypes3", challengeTypes3 }, { "challengegeneration", challengeGeneration }, { "challengelights", challengeLights }, { "editedfiles", editedFiles }, { "modserver", modServer }, { "gamebanana", gameBanana },
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
