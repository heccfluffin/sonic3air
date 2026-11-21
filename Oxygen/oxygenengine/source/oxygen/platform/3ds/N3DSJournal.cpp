/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the journal in the "3DS" menu - a stamp card for each day played, and a few numbers.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSJournal.h"
#include "oxygen/platform/3ds/N3DSFileWriter.h"
#include "oxygen/platform/3ds/N3DSNotificationLight.h"
#include "oxygen/platform/3ds/N3DSProfile.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <vector>


namespace n3ds
{
	namespace journal
	{
		namespace
		{
			const char* FILE_PATH = "sdmc:/3ds/sonic3air/savedata/3ds_journal.txt";

			Data gData;
			std::string gSavedText;			// As last loaded / saved, to save only if something changed
			bool gLoaded = false;
			bool gNewStampNotice = false;

			uint64_t gLastTick = 0;
			uint64_t gPlayTicks = 0;		// Not counted in whole seconds yet
			uint64_t gNextDayCheck = 0;
			uint64_t gNextSave = 0;

			bool gPtmInitialized = false;

			uint32_t today()
			{
				const n3ds::profile::LocalTime now = n3ds::profile::getLocalTime();
				return (uint32_t)(now.mYear * 10000 + now.mMonth * 100 + now.mDay);
			}

			std::string toText(const Data& data)
			{
				char text[512];
				snprintf(text, sizeof(text),
						 "started=%u\nlast_day=%u\ndays_played=%u\nstamps=%d\ncards_completed=%u\nplay_seconds=%llu\nplay_coins_spent=%u\nthings_bought=%u\nscreenshots=%u\n",
						 (unsigned)data.mStartedDate, (unsigned)data.mLastDayPlayed, (unsigned)data.mDaysPlayed, data.mStamps, (unsigned)data.mCardsCompleted,
						 (unsigned long long)data.mPlaySeconds, (unsigned)data.mPlayCoinsSpent, (unsigned)data.mThingsBought, (unsigned)data.mScreenshots);
				return text;
			}

			void load()
			{
				if (gLoaded)
					return;
				gLoaded = true;

				FILE* file = fopen(FILE_PATH, "r");
				if (nullptr != file)
				{
					char line[128];
					while (nullptr != fgets(line, sizeof(line), file))
					{
						unsigned int value = 0;
						unsigned long long bigValue = 0;
						if (sscanf(line, "started=%u", &value) == 1)				gData.mStartedDate = value;
						else if (sscanf(line, "last_day=%u", &value) == 1)			gData.mLastDayPlayed = value;
						else if (sscanf(line, "days_played=%u", &value) == 1)		gData.mDaysPlayed = value;
						else if (sscanf(line, "stamps=%u", &value) == 1)			gData.mStamps = (int)std::min<unsigned int>(value, STAMPS_PER_CARD);
						else if (sscanf(line, "cards_completed=%u", &value) == 1)	gData.mCardsCompleted = value;
						else if (sscanf(line, "play_seconds=%llu", &bigValue) == 1)	gData.mPlaySeconds = bigValue;
						else if (sscanf(line, "play_coins_spent=%u", &value) == 1)	gData.mPlayCoinsSpent = value;
						else if (sscanf(line, "things_bought=%u", &value) == 1)		gData.mThingsBought = value;
						else if (sscanf(line, "screenshots=%u", &value) == 1)		gData.mScreenshots = value;
					}
					fclose(file);
					gSavedText = toText(gData);
				}
				if (gData.mStartedDate == 0)
					gData.mStartedDate = today();		// A new journal
			}

			void checkDay()
			{
				const uint32_t day = today();
				if (day == gData.mLastDayPlayed)
					return;

				// The first time the game runs on this day: the day's stamp. Days don't have to follow each other, skipped days are fine.
				gData.mLastDayPlayed = day;
				++gData.mDaysPlayed;
				if (gData.mStamps >= STAMPS_PER_CARD)
					gData.mStamps = 0;		// The previous card was full: a fresh one
				++gData.mStamps;
				if (gData.mStamps == STAMPS_PER_CARD)
					++gData.mCardsCompleted;
				gNewStampNotice = true;

				n3ds::notificationlight::pulse(n3ds::notificationlight::Color::PINK);
				RMX_LOG_INFO("Journal: stamp " << gData.mStamps << " of " << STAMPS_PER_CARD << " for today (" << gData.mDaysPlayed << " days played)");
				save();
			}
		}

		const Data& get()
		{
			load();
			return gData;
		}

		void update()
		{
			load();
			const uint64_t now = svcGetSystemTick();

			// Play time: gaps of more than half a second (HOME Menu, sleep mode) don't count
			if (gLastTick != 0)
			{
				const uint64_t delta = now - gLastTick;
				if (delta < SYSCLOCK_ARM11 / 2)
				{
					gPlayTicks += delta;
					if (gPlayTicks >= SYSCLOCK_ARM11)
					{
						gData.mPlaySeconds += gPlayTicks / SYSCLOCK_ARM11;
						gPlayTicks %= SYSCLOCK_ARM11;
					}
				}
			}
			gLastTick = now;

			// A new day (also when playing past midnight)
			if (now >= gNextDayCheck)
			{
				gNextDayCheck = now + 2 * (uint64_t)SYSCLOCK_ARM11;
				checkDay();
			}

			// Save now and then, for the play time
			if (gNextSave == 0)
				gNextSave = now + 60 * (uint64_t)SYSCLOCK_ARM11;
			else if (now >= gNextSave)
			{
				gNextSave = now + 60 * (uint64_t)SYSCLOCK_ARM11;
				save();
			}
		}

		void save()
		{
			if (!gLoaded)
				return;
			const std::string text = toText(gData);
			if (text == gSavedText)
				return;
			gSavedText = text;
			n3ds::filewriter::writeFile(FILE_PATH, std::vector<uint8_t>(text.begin(), text.end()), [](bool success, uint32_t)
			{
				if (!success)
					RMX_LOG_INFO("Journal: could not save " << FILE_PATH);
			});
		}

		bool takeNewStampNotice()
		{
			const bool notice = gNewStampNotice;
			gNewStampNotice = false;
			return notice;
		}

		void addPurchase(int playCoins)
		{
			load();
			gData.mPlayCoinsSpent += (uint32_t)std::max(playCoins, 0);
			++gData.mThingsBought;
			save();
		}

		void addScreenshot()
		{
			load();
			++gData.mScreenshots;
			save();
		}

		bool getSteps(uint32_t& outTotal)
		{
			// Only the total: libctru's PTMU_GetStepHistory (for "steps today") describes its buffer with a size the PTM service
			// doesn't expect (the Azahar emulator stops on that, expecting 2 bytes per hour), so it's not used
			if (!gPtmInitialized)
				gPtmInitialized = R_SUCCEEDED(ptmuInit());
			if (!gPtmInitialized)
				return false;
			u32 total = 0;
			if (R_FAILED(PTMU_GetTotalStepCount(&total)))
				return false;
			outTotal = total;
			return true;
		}

		std::string formatDate(uint32_t yyyymmdd)
		{
			static const char* MONTHS[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
			const int year = (int)(yyyymmdd / 10000);
			const int month = (int)(yyyymmdd / 100 % 100);
			const int day = (int)(yyyymmdd % 100);
			char text[32];
			snprintf(text, sizeof(text), "%d %s %d", day, (month >= 1 && month <= 12) ? MONTHS[month - 1] : "?", year);
			return text;
		}
	}
}

#endif
