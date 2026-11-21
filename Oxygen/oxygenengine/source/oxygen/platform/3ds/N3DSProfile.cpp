/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the player's 3DS profile (nickname, birthday) and the clock, for greetings and the time of day.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSProfile.h"

#include <3ds.h>
#include <time.h>


namespace n3ds
{
	namespace profile
	{
		namespace
		{
			bool gLoaded = false;
			std::string gNickname;
			int gBirthdayMonth = 0;
			int gBirthdayDay = 0;

			void load()
			{
				if (gLoaded)
					return;
				gLoaded = true;
				if (R_FAILED(cfguInit()))
				{
					RMX_LOG_INFO("3DS profile: could not read the system settings");
					return;
				}

				// Nickname: up to 10 UTF-16 characters
				u16 name[0x1c / 2] = { 0 };
				if (R_SUCCEEDED(CFGU_GetConfigInfoBlk2(0x1c, 0x000a0000, name)))
				{
					std::string nickname;
					bool printable = true;
					for (int i = 0; i < 10 && name[i] != 0; ++i)
					{
						if (name[i] < 0x20 || name[i] > 0x7e)
						{
							printable = false;		// The game's fonts only have the basic characters, rather leave the name out
							break;
						}
						nickname += (char)name[i];
					}
					while (!nickname.empty() && nickname.back() == ' ')
						nickname.pop_back();
					if (printable)
						gNickname = nickname;
				}

				// Birthday: month, day
				u8 birthday[2] = { 0, 0 };
				if (R_SUCCEEDED(CFGU_GetConfigInfoBlk2(2, 0x000a0001, birthday)) && birthday[0] >= 1 && birthday[0] <= 12 && birthday[1] >= 1 && birthday[1] <= 31)
				{
					gBirthdayMonth = birthday[0];
					gBirthdayDay = birthday[1];
				}
				cfguExit();
			}
		}

		const std::string& getNickname()
		{
			load();
			return gNickname;
		}

		bool getBirthday(int& outMonth, int& outDay)
		{
			load();
			outMonth = gBirthdayMonth;
			outDay = gBirthdayDay;
			return (gBirthdayMonth != 0);
		}

		LocalTime getLocalTime()
		{
			// The 3DS clock is local time, without a time zone, so "gmtime" gives the time as shown on the 3DS
			const time_t seconds = time(nullptr);
			struct tm local;
			gmtime_r(&seconds, &local);
			LocalTime result;
			result.mYear = local.tm_year + 1900;
			result.mMonth = local.tm_mon + 1;
			result.mDay = local.tm_mday;
			result.mHour = local.tm_hour;
			result.mMinute = local.tm_min;
			result.mDayOfYear = local.tm_yday;
			return result;
		}

		bool isBirthdayToday()
		{
			int month, day;
			if (!getBirthday(month, day))
				return false;
			const LocalTime now = getLocalTime();
			return (now.mMonth == month && now.mDay == day);
		}

		std::string getGreeting()
		{
			const std::string& name = getNickname();
			const std::string comma = name.empty() ? std::string() : (", " + name);
			if (isBirthdayToday())
				return "Happy birthday" + comma + "!";

			const LocalTime now = getLocalTime();
			if (now.mHour >= 5 && now.mHour < 12)
				return "Good morning" + comma + "!";
			if (now.mHour >= 12 && now.mHour < 17)
				return "Good afternoon" + comma + "!";
			if (now.mHour >= 17 && now.mHour < 22)
				return "Good evening" + comma + "!";
			return name.empty() ? std::string("Playing late? Don't forget to rest!") : ("Playing late, " + name + "? Don't forget to rest!");
		}
	}
}

#endif
