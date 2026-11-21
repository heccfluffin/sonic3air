/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the player's 3DS profile (nickname, birthday) and the clock, for greetings and the time of day.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include <string>


namespace n3ds
{
	namespace profile
	{
		// The nickname from the 3DS system settings, or empty if it can't be shown with the game's fonts (only plain
		// letters, digits and simple punctuation are kept as they are)
		const std::string& getNickname();

		// Birthday from the 3DS system settings; false if not set
		bool getBirthday(int& outMonth, int& outDay);

		// Local time as shown on the 3DS
		struct LocalTime
		{
			int mYear = 2000;
			int mMonth = 1;			// 1-12
			int mDay = 1;			// 1-31
			int mHour = 0;			// 0-23
			int mMinute = 0;
			int mDayOfYear = 0;		// 0-365
		};
		LocalTime getLocalTime();

		bool isBirthdayToday();

		// 12 hours with AM / PM on American 3DS systems, 24 hours for the others (like the clock on the bottom screen)
		bool uses12HourClock();
		std::string formatTime(int minutesAfterMidnight);		// "8:30 PM" or "20:30"

		// "Good morning, Name!" and the like, depending on the time of day (and the birthday)
		std::string getGreeting();
	}
}

#endif
