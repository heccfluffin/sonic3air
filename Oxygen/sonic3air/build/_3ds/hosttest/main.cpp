// Host test for the 3DS port's bottom screen, 3DS menu data, profile and background file writer.
// Runs the real 3DS code against a fake 3DS (fake3ds.cpp) and saves what the bottom screen shows as pictures.
// Usage: hosttest <scenario> <output folder>   (run.sh runs all scenarios, each in its own empty folder as the "SD card")

#include "fake/3ds.h"
#include "fake/oxygen/pch.h"
#include "fake3ds.h"

#include "oxygen/platform/3ds/N3DSBottomScreen.h"
#include "oxygen/platform/3ds/N3DSChallenges.h"
#include "oxygen/platform/3ds/N3DSFileWriter.h"
#include "oxygen/platform/3ds/N3DSJournal.h"
#include "oxygen/platform/3ds/N3DSProfile.h"
#include "oxygen/platform/3ds/N3DSShop.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <map>
#include <memory>
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

	void challenges()
	{
		using namespace n3ds::challenges;
		FTX::FileSystem->createDirectory(L"sdmc:/3ds/sonic3air/savedata");

		// Today's challenges, set up by hand: 50 rings in Hydrocity, Angel Island 1 in under 2:30, Hydrocity without a hit
		fake::setTime(2026, 11, 23, 18, 0);
		std::ofstream("sdmc:/3ds/sonic3air/savedata/3ds_challenges.txt") << "date=20261123\nxp=90\nbeaten=9\nperfect_days=2\nzones=15\n"
			"challenge=0 1 0 50 0 0\nchallenge=4 0 0 150 0 0\nchallenge=6 1 0 0 0 0\n";
		const Data& data = get(15);
		check(data.mDate == 20261123 && data.mXp == 90 && data.mChallenges[0].mType == Type::RINGS_IN_ZONE, "today's challenges are read back");
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
		check(data.mChallenges[0].mDone && data.mXp == 100 && getLevel(data.mXp) == 2, "50 rings: beaten, 10 XP, medal level 2");
		check(takeBeatenNotice() == 0 && takeLevelUpNotice() && n3ds::journal::get().mVouchers == 1, "... with a Robotnik voucher for the new level");

		onActCompleted(0, 0, 10, 160, false);
		check(!data.mChallenges[1].mDone && describeProgress(data.mChallenges[1]) == "Best: 2:40", "Angel Island 1 in 2:40 is too slow");
		onActCompleted(0, 1, 10, 100, false);
		check(!data.mChallenges[1].mDone, "... and act 2 doesn't count");
		onActCompleted(0, 0, 10, 140, false);
		check(data.mChallenges[1].mDone && data.mXp == 120, "2:20: beaten, 20 XP");

		onActCompleted(1, 1, 0, 100, true);
		check(!data.mChallenges[2].mDone, "a hit in Hydrocity: not beaten");
		onActCompleted(1, 0, 0, 100, false);
		check(data.mChallenges[2].mDone && data.mXp == 150 && data.mPerfectDays == 3 && data.mBeaten == 12, "without a hit: beaten, 30 XP, a perfect day");

		n3ds::filewriter::waitUntilDone();
		const std::string saved = readFile("sdmc:/3ds/sonic3air/savedata/3ds_challenges.txt");
		check(saved.find("xp=150\n") != std::string::npos && saved.find("challenge=6 1 0 0 0 1\n") != std::string::npos, "saved right away");

		// The next day: new challenges, the medal stays
		fake::setTime(2026, 11, 24, 9, 0);
		get();
		check(data.mDate == 20261124 && data.mXp == 150 && !data.mChallenges[0].mDone && !data.mChallenges[1].mDone && !data.mChallenges[2].mDone,
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
		check(std::string(getRank(1)) == "Rookie" && std::string(getRank(99)) == "Robotnik's Nightmare", "medal ranks");
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
		{ "filewriter", fileWriter }, { "shopreload", shopReload }, { "postcards", postcards }, { "postcardsall", postcardsAll }, { "rewards", rewards }, { "rewardsreload", rewardsReload }, { "challenges", challenges },
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
