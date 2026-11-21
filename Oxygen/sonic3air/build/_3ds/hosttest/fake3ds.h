// Host test: controls for the fake 3DS (see fake/3ds.h)
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace fake
{
	void setTime(int year, int month, int day, int hour, int minute);
	void advanceSeconds(double seconds);		// Moves the system tick and the clock on
	void setBattery(int level, bool charging);
	void setRegion(int region);
	void setNickname(const std::u16string& name);
	void setBirthday(int month, int day);
	void setTouch(bool touching, int x = 0, int y = 0);		// For the next "hidScanInput"
	void setSDCardDelay(int ms);					// Extra time each write to the SD card takes

	int getSwapCount();							// How often the bottom screen was presented
	bool saveScreenshot(const std::string& filename);	// Bottom screen as a 320 x 240 PPM picture
	const std::vector<std::string>& getLog();
}
