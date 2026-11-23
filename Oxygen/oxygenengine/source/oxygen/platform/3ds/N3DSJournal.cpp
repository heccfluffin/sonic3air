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
			bool gNewPostcardNotice = false;
			bool gNewRewardNotice = false;

			// The zone preview pictures there are: acts per zone, for zones 1 to 14 (Hidden Palace, Sky Sanctuary and Doomsday have one)
			const int ACTS_PER_ZONE[14] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 2, 1 };

			// The funny postcards, by name (the 3DS menu has their scenes and texts); new ones only ever go at the end
			const char* FUNNY_POSTCARDS[] = { "staydry", "snowday", "photobomb", "sleepy", "wrongmonitor", "decoys", "speedlimit", "rolling",
											  "skyhigh", "warm", "scenic", "carnival", "sinking", "airship", "bubble",
											  "coldcall", "heyho", "highfive", "bigone", "spintop", "firecraft" };
			const int NUM_FUNNY = (int)(sizeof(FUNNY_POSTCARDS) / sizeof(FUNNY_POSTCARDS[0]));

			bool hasFunny(int index);

			uint64_t gLastTick = 0;
			uint64_t gPlayTicks = 0;		// Not counted in whole seconds yet
			uint64_t gNextDayCheck = 0;
			uint64_t gNextSave = 0;
			uint32_t gGameDistance = 0;		// Toward the next step, in 1/256 pixels

			uint32_t today()
			{
				const n3ds::profile::LocalTime now = n3ds::profile::getLocalTime();
				return (uint32_t)(now.mYear * 10000 + now.mMonth * 100 + now.mDay);
			}

			std::string toText(const Data& data)
			{
				char text[512];
				snprintf(text, sizeof(text),
						 "started=%u\nlast_day=%u\ndays_played=%u\nstamps=%d\ncards_completed=%u\nplay_seconds=%llu\nplay_coins_spent=%u\nthings_bought=%u\nscreenshots=%u\n"
						 "rewarded_cards=%u\nvouchers=%u\nbonus_boxes=%u\nbonus_ready=%d\nwelcome_postcard=%d\ngame_steps=%llu\n",
						 (unsigned)data.mStartedDate, (unsigned)data.mLastDayPlayed, (unsigned)data.mDaysPlayed, data.mStamps, (unsigned)data.mCardsCompleted,
						 (unsigned long long)data.mPlaySeconds, (unsigned)data.mPlayCoinsSpent, (unsigned)data.mThingsBought, (unsigned)data.mScreenshots,
						 (unsigned)data.mRewardedCards, (unsigned)data.mVouchers, (unsigned)data.mBonusBoxes, data.mBonusReady, data.mWelcomePostcard ? 1 : 0, (unsigned long long)data.mGameSteps);
				std::string result = text;
				for (const Postcard& postcard : data.mPostcards)
				{
					snprintf(text, sizeof(text), "postcard=%s %u\n", postcard.getSpriteName().c_str(), (unsigned)postcard.mDate);
					result += text;
				}
				return result;
			}

			bool hasPostcard(int zone, int act, char image)
			{
				for (const Postcard& postcard : gData.mPostcards)
				{
					if (postcard.mFunny < 0 && postcard.mZone == zone && postcard.mAct == act && postcard.mImage == image)
						return true;
				}
				return false;
			}

			bool hasFunny(int index)
			{
				for (const Postcard& postcard : gData.mPostcards)
				{
					if (postcard.mFunny == index)
						return true;
				}
				return false;
			}

			bool hasZone(int zone)
			{
				for (const Postcard& postcard : gData.mPostcards)
				{
					if (postcard.mFunny < 0 && postcard.mZone == zone)
						return true;
				}
				return false;
			}

			// A random picture not collected yet, preferring zones without any postcard (and funny ones mixed in); false once all are collected
			bool addPostcard()
			{
				std::vector<Postcard> candidates;
				for (int index = 0; index < NUM_FUNNY; ++index)
				{
					if (!hasFunny(index))
					{
						Postcard postcard;
						postcard.mFunny = index;
						candidates.push_back(postcard);
					}
				}
				for (int pass = 0; pass < 2; ++pass)
				{
					// More pictures of zones already visited only once no new zone (or funny one) is left
					if (pass == 1 && !candidates.empty())
						break;
					for (int zone = 1; zone <= 14; ++zone)
					{
						if (pass == 0 && hasZone(zone))
							continue;
						for (int act = 1; act <= ACTS_PER_ZONE[zone - 1]; ++act)
						{
							for (char image : { 'a', 'b' })
							{
								if (hasPostcard(zone, act, image))
									continue;
								Postcard postcard;
								postcard.mZone = zone;
								postcard.mAct = act;
								postcard.mImage = image;
								candidates.push_back(postcard);
							}
						}
					}
				}
				if (candidates.empty())
					return false;

				static uint32_t random = 0;
				if (random == 0)
					random = (uint32_t)svcGetSystemTick() | 1;
				random = random * 1103515245u + 12345u;
				Postcard postcard = candidates[(random >> 8) % candidates.size()];
				postcard.mDate = today();
				gData.mPostcards.push_back(postcard);
				gNewPostcardNotice = true;
				RMX_LOG_INFO("Journal: a postcard arrived, " << postcard.getSpriteName() << " (" << gData.mPostcards.size() << " of " << getNumPossiblePostcards() << ")");
				return true;
			}

			// Postcards there should be: one per completed card, and the welcome one
			size_t getPostcardsDue()
			{
				return (size_t)gData.mCardsCompleted + (gData.mWelcomePostcard ? 1 : 0);
			}

			// One postcard per completed card (also for cards completed before there were postcards)
			void catchUpPostcards()
			{
				while (gData.mPostcards.size() < getPostcardsDue() && (int)gData.mPostcards.size() < getNumPossiblePostcards())
				{
					if (!addPostcard())
						break;
				}
			}

			// A voucher and a bonus box per completed card
			void giveRewards()
			{
				while (gData.mRewardedCards < gData.mCardsCompleted)
				{
					++gData.mRewardedCards;
					++gData.mVouchers;
					++gData.mBonusBoxes;
					gNewRewardNotice = true;
					RMX_LOG_INFO("Journal: a voucher and a bonus box for card " << gData.mRewardedCards);
				}
			}

			void load()
			{
				if (gLoaded)
					return;
				gLoaded = true;

				bool hasRewardedCards = false;
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
						else if (sscanf(line, "rewarded_cards=%u", &value) == 1)	{ gData.mRewardedCards = value;  hasRewardedCards = true; }
						else if (sscanf(line, "vouchers=%u", &value) == 1)			gData.mVouchers = value;
						else if (sscanf(line, "bonus_boxes=%u", &value) == 1)		gData.mBonusBoxes = value;
						else if (sscanf(line, "game_steps=%llu", &bigValue) == 1)	gData.mGameSteps = bigValue;
						else if (sscanf(line, "welcome_postcard=%u", &value) == 1)	gData.mWelcomePostcard = (value != 0);
						else if (sscanf(line, "bonus_ready=%u", &value) == 1)		gData.mBonusReady = (value < _NUM_BONUS_GIFTS) ? (int)value : BONUS_NONE;
						else
						{
							int zone = 0;
							int act = 0;
							char image = 0;
							char funnyName[32] = { 0 };
							if (sscanf(line, "postcard=funny_%31s %u", funnyName, &value) == 2)
							{
								for (int index = 0; index < NUM_FUNNY; ++index)
								{
									if (strcmp(funnyName, FUNNY_POSTCARDS[index]) == 0 && !hasFunny(index))
									{
										Postcard postcard;
										postcard.mFunny = index;
										postcard.mDate = value;
										gData.mPostcards.push_back(postcard);
									}
								}
							}
							else if (sscanf(line, "postcard=zone%2d_act%1d%c %u", &zone, &act, &image, &value) == 4 && zone >= 1 && zone <= 14 &&
								act >= 1 && act <= ACTS_PER_ZONE[zone - 1] && (image == 'a' || image == 'b') && !hasPostcard(zone, act, image))
							{
								Postcard postcard;
								postcard.mZone = zone;
								postcard.mAct = act;
								postcard.mImage = image;
								postcard.mDate = value;
								gData.mPostcards.push_back(postcard);
							}
						}
					}
					fclose(file);
					gSavedText = toText(gData);
				}
				if (gData.mStartedDate == 0)
					gData.mStartedDate = today();		// A new journal
				if (!hasRewardedCards && gData.mCardsCompleted > 0)
				{
					// A journal from before there were vouchers and bonus boxes: the rewards of the latest card, as a welcome
					gData.mRewardedCards = gData.mCardsCompleted - 1;
				}
				if (gData.mPostcards.size() < getPostcardsDue() || gData.mRewardedCards < gData.mCardsCompleted)
				{
					catchUpPostcards();
					giveRewards();
					save();
				}
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
				{
					++gData.mCardsCompleted;
					catchUpPostcards();		// The card's postcard
					giveRewards();
				}
				gNewStampNotice = true;

				n3ds::notificationlight::pulse(n3ds::notificationlight::Color::PINK);
				RMX_LOG_INFO("Journal: stamp " << gData.mStamps << " of " << STAMPS_PER_CARD << " for today (" << gData.mDaysPlayed << " days played)");

				// Saved a bit later, not right away: the day's first start is usually this, and while the SD card writes the file
				// (about 2 seconds on a slow card), everything else that reads from it waits - the title screen froze for that long.
				// Quitting before then still saves it (see EngineMain).
				gNextSave = svcGetSystemTick() + 15 * (uint64_t)SYSCLOCK_ARM11;
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

		std::string Postcard::getSpriteName() const
		{
			if (mFunny >= 0)
				return std::string("funny_") + ((mFunny < NUM_FUNNY) ? FUNNY_POSTCARDS[mFunny] : "unknown");
			char name[16];
			snprintf(name, sizeof(name), "zone%02d_act%d%c", mZone, mAct, mImage);
			return name;
		}

		int getNumPossiblePostcards()
		{
			int count = NUM_FUNNY;
			for (int acts : ACTS_PER_ZONE)
				count += acts * 2;
			return count;
		}

		int getNumFunnyPostcards()
		{
			return NUM_FUNNY;
		}

		const char* getFunnyPostcardName(int index)
		{
			return (index >= 0 && index < NUM_FUNNY) ? FUNNY_POSTCARDS[index] : nullptr;
		}

		bool giveWelcomePostcard()
		{
			load();
			if (gData.mWelcomePostcard)
				return false;
			gData.mWelcomePostcard = true;
			const bool added = addPostcard();
			gNewPostcardNotice = false;		// It's shown right away

			// Saved a bit later, not right away: the album loads the postcard's picture now, and while the SD card writes the file
			// (about 2 seconds on a slow card), reading anything else from it waits - the menu froze for that long
			gNextSave = svcGetSystemTick() + 15 * (uint64_t)SYSCLOCK_ARM11;
			return added;
		}

		bool takeNewRewardNotice()
		{
			const bool notice = gNewRewardNotice;
			gNewRewardNotice = false;
			return notice;
		}

		bool useVoucher()
		{
			load();
			if (gData.mVouchers == 0)
				return false;
			--gData.mVouchers;
			save();
			return true;
		}

		void addVouchers(int count)
		{
			load();
			if (count <= 0)
				return;
			gData.mVouchers += (uint32_t)count;
			save();
		}

		bool prepareBonus(int gift)
		{
			load();
			if (gift <= BONUS_NONE || gift >= _NUM_BONUS_GIFTS)
				return false;
			if (gData.mBonusReady == BONUS_NONE)
			{
				if (gData.mBonusBoxes == 0)
					return false;
				--gData.mBonusBoxes;
			}
			gData.mBonusReady = gift;
			save();
			return true;
		}

		void bonusGiven()
		{
			load();
			gData.mBonusReady = BONUS_NONE;
			// Saved with the next regular save (within a minute, and at exit) - not in the middle of the act's start
		}

		bool takeNewPostcardNotice()
		{
			const bool notice = gNewPostcardNotice;
			gNewPostcardNotice = false;
			return notice;
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

		void addGameDistance(uint32_t subpixels)
		{
			// Saved with the play time, once a minute
			load();
			gGameDistance += subpixels;
			const uint32_t stepLength = GAME_STEP_PIXELS * 256;
			if (gGameDistance >= stepLength)
			{
				gData.mGameSteps += gGameDistance / stepLength;
				gGameDistance %= stepLength;
			}
		}

		void addScreenshot()
		{
			load();
			++gData.mScreenshots;
			save();
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
