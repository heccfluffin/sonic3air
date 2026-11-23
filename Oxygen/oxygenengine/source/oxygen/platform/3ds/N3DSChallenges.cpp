/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: Doctor Robotnik's Daily Challenge in the "3DS" menu - three new challenges every day (easy, medium,
*	hard), and a Robotnik medal that levels up with each one beaten. Stored in "savedata/3ds_challenges.txt".
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSChallenges.h"
#include "oxygen/platform/3ds/N3DSFileWriter.h"
#include "oxygen/platform/3ds/N3DSJournal.h"
#include "oxygen/platform/3ds/N3DSNotificationLight.h"
#include "oxygen/platform/3ds/N3DSProfile.h"

#include <3ds.h>
#include <stdio.h>
#include <vector>


namespace n3ds
{
	namespace challenges
	{
		namespace
		{
			const char* FILE_PATH = "sdmc:/3ds/sonic3air/savedata/3ds_challenges.txt";
			const int NUM_ZONES = 12;		// Angel Island to Death Egg, in the game's numbering (Hidden Palace and Doomsday aren't used)

			struct ZoneInfo
			{
				const char* mName;
				int mActs;
				uint32_t mParTime[2];		// Seconds: a relaxed time for a first try
			};
			const ZoneInfo ZONES[NUM_ZONES] =
			{
				{ "Angel Island",	2, { 150, 210 } },
				{ "Hydrocity",		2, { 180, 240 } },
				{ "Marble Garden",	2, { 210, 270 } },
				{ "Carnival Night",	2, { 180, 240 } },
				{ "Flying Battery",	2, { 210, 240 } },
				{ "IceCap",			2, { 180, 240 } },
				{ "Launch Base",	2, { 210, 270 } },
				{ "Mushroom Hill",	2, { 210, 240 } },
				{ "Sandopolis",		2, { 270, 330 } },
				{ "Lava Reef",		2, { 210, 180 } },
				{ "Sky Sanctuary",	1, { 180,   0 } },
				{ "Death Egg",		2, { 210, 240 } },
			};

			Data gData;
			std::string gSavedText;
			bool gLoaded = false;
			uint32_t gZoneMask = 0;			// Finished acts, as last given
			uint32_t gStepDistance = 0;		// Toward the next step, in 1/256 pixels
			int gBeatenNotice = -1;
			bool gLevelUpNotice = false;
			bool gDirty = false;			// Progress changed, saved now and then
			uint64_t gNextDayCheck = 0;
			uint64_t gNextSave = 0;

			uint32_t today()
			{
				const n3ds::profile::LocalTime now = n3ds::profile::getLocalTime();
				return (uint32_t)(now.mYear * 10000 + now.mMonth * 100 + now.mDay);
			}

			std::string toText(const Data& data)
			{
				char text[160];
				snprintf(text, sizeof(text), "date=%u\nxp=%u\nbeaten=%u\nperfect_days=%u\nzones=%u\n",
						 (unsigned)data.mDate, (unsigned)data.mXp, (unsigned)data.mBeaten, (unsigned)data.mPerfectDays, (unsigned)gZoneMask);
				std::string result = text;
				for (const Challenge& challenge : data.mChallenges)
				{
					snprintf(text, sizeof(text), "challenge=%d %d %d %u %u %d\n", (int)challenge.mType, (int)challenge.mZone, (int)challenge.mAct,
							 (unsigned)challenge.mTarget, (unsigned)challenge.mProgress, challenge.mDone ? 1 : 0);
					result += text;
				}
				return result;
			}

			bool isZoneAvailable(int zone)
			{
				return (zone == 0) || ((gZoneMask >> (zone * 2)) & 3) != 0;
			}

			// Chooses the day's challenges: the same for the same day and zones
			void generate(uint32_t date)
			{
				uint32_t random = date * 2654435761u + 0x9e3779b9u;
				const auto next = [&random](uint32_t range)
				{
					random = random * 1103515245u + 12345u;
					return (range == 0) ? 0 : (random >> 8) % range;
				};

				std::vector<uint8_t> zones;
				for (int zone = 0; zone < NUM_ZONES; ++zone)
				{
					if (isZoneAvailable(zone))
						zones.push_back((uint8_t)zone);
				}
				const auto pickZone = [&]()  { return zones[next((uint32_t)zones.size())]; };
				const auto pickAct = [&](uint8_t zone) -> uint8_t
				{
					// An act already finished, if there's one (so it can be played in Act Select)
					const uint32_t finished = (gZoneMask >> (zone * 2)) & 3;
					if (ZONES[zone].mActs < 2 || finished == 1)
						return 0;
					if (finished == 2)
						return 1;
					return (uint8_t)next(2);
				};

				Data& data = gData;
				data.mDate = date;
				for (int index = 0; index < NUM_PER_DAY; ++index)
				{
					Challenge challenge;
					const uint32_t kind = next(3);
					if (index == 0)
					{
						// Easy
						if (kind == 0)
						{
							challenge.mType = Type::RINGS_IN_ZONE;
							challenge.mZone = pickZone();
							challenge.mTarget = (next(2) == 0) ? 30 : 50;
						}
						else if (kind == 1)
						{
							challenge.mType = Type::FINISH_ZONE;
							challenge.mZone = pickZone();
						}
						else
						{
							challenge.mType = Type::SONIC_STEPS;
							challenge.mTarget = 500 + 250 * next(3);
						}
					}
					else if (index == 1)
					{
						// Medium
						if (kind == 0)
						{
							challenge.mType = Type::RINGS_AT_ONCE;
							challenge.mTarget = 100;
						}
						else if (kind == 1)
						{
							challenge.mType = Type::ACT_UNDER_TIME;
							challenge.mZone = pickZone();
							challenge.mAct = pickAct(challenge.mZone);
							challenge.mTarget = ZONES[challenge.mZone].mParTime[challenge.mAct];
						}
						else
						{
							challenge.mType = Type::FINISH_WITH_RINGS;
							challenge.mZone = pickZone();
							challenge.mTarget = 50;
						}
					}
					else
					{
						// Hard
						if (kind == 0)
						{
							challenge.mType = Type::NO_HIT_ACT;
							challenge.mZone = pickZone();
						}
						else if (kind == 1)
						{
							challenge.mType = Type::RINGS_AT_ONCE;
							challenge.mTarget = 200;
						}
						else
						{
							challenge.mType = Type::ACT_UNDER_TIME;
							challenge.mZone = pickZone();
							challenge.mAct = pickAct(challenge.mZone);
							challenge.mTarget = ZONES[challenge.mZone].mParTime[challenge.mAct] * 3 / 4 / 10 * 10;		// A quarter faster
						}
					}
					data.mChallenges[index] = challenge;
				}
				gStepDistance = 0;
				RMX_LOG_INFO("Daily challenge for " << date << ": " << describe(data.mChallenges[0]) << " / " << describe(data.mChallenges[1]) << " / " << describe(data.mChallenges[2]));
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
					int index = 0;
					while (nullptr != fgets(line, sizeof(line), file))
					{
						unsigned int value = 0;
						int type = 0, zone = 0, act = 0, done = 0;
						unsigned int target = 0, progress = 0;
						if (sscanf(line, "date=%u", &value) == 1)				gData.mDate = value;
						else if (sscanf(line, "xp=%u", &value) == 1)			gData.mXp = value;
						else if (sscanf(line, "beaten=%u", &value) == 1)		gData.mBeaten = value;
						else if (sscanf(line, "perfect_days=%u", &value) == 1)	gData.mPerfectDays = value;
						else if (sscanf(line, "zones=%u", &value) == 1)			gZoneMask = value;
						else if (sscanf(line, "challenge=%d %d %d %u %u %d", &type, &zone, &act, &target, &progress, &done) == 6 && index < NUM_PER_DAY &&
								 type >= 0 && type < (int)Type::_NUM && zone >= 0 && zone < NUM_ZONES && act >= 0 && act < ZONES[zone].mActs)
						{
							Challenge& challenge = gData.mChallenges[index++];
							challenge.mType = (Type)type;
							challenge.mZone = (uint8_t)zone;
							challenge.mAct = (uint8_t)act;
							challenge.mTarget = target;
							challenge.mProgress = progress;
							challenge.mDone = (done != 0);
						}
					}
					fclose(file);
					if (index < NUM_PER_DAY)
						gData.mDate = 0;		// Incomplete: new challenges
					gSavedText = toText(gData);
				}
			}

			void checkDay()
			{
				const uint32_t day = today();
				if (day != gData.mDate)
				{
					generate(day);
					save();
				}
			}

			void beat(int index)
			{
				Challenge& challenge = gData.mChallenges[index];
				if (challenge.mDone)
					return;
				challenge.mDone = true;
				const int levelBefore = getLevel(gData.mXp);
				gData.mXp += (uint32_t)getXpFor(index);
				++gData.mBeaten;
				bool allDone = true;
				for (const Challenge& other : gData.mChallenges)
					allDone = allDone && other.mDone;
				if (allDone)
					++gData.mPerfectDays;

				const int levelsGained = getLevel(gData.mXp) - levelBefore;
				if (levelsGained > 0)
				{
					n3ds::journal::addVouchers(levelsGained);		// A Robotnik voucher for every medal level
					gLevelUpNotice = true;
				}
				gBeatenNotice = index;
				n3ds::notificationlight::pulse(n3ds::notificationlight::Color::RED);
				RMX_LOG_INFO("Daily challenge beaten: " << describe(challenge) << " (medal level " << getLevel(gData.mXp) << ", " << gData.mXp << " XP)");
				save();
			}
		}

		const Data& get(uint32_t finishedZoneActs)
		{
			load();
			gZoneMask = finishedZoneActs;
			checkDay();
			return gData;
		}

		const Data& get()
		{
			load();
			checkDay();
			return gData;
		}

		void save()
		{
			if (!gLoaded)
				return;
			gDirty = false;
			gNextSave = 0;
			const std::string text = toText(gData);
			if (text == gSavedText)
				return;
			gSavedText = text;
			n3ds::filewriter::writeFile(FILE_PATH, std::vector<uint8_t>(text.begin(), text.end()), [](bool success, uint32_t)
			{
				if (!success)
					RMX_LOG_INFO("Daily challenge: could not save " << FILE_PATH);
			});
		}

		bool onFrame(const Frame& frame)
		{
			load();
			const int beatenBefore = (int)gData.mBeaten;
			const uint64_t now = svcGetSystemTick();
			if (now >= gNextDayCheck)
			{
				gNextDayCheck = now + 2 * (uint64_t)SYSCLOCK_ARM11;
				checkDay();
			}

			for (int index = 0; index < NUM_PER_DAY; ++index)
			{
				Challenge& challenge = gData.mChallenges[index];
				if (challenge.mDone)
					continue;
				switch (challenge.mType)
				{
					case Type::RINGS_IN_ZONE:
					case Type::RINGS_AT_ONCE:
						if (challenge.mType == Type::RINGS_IN_ZONE && frame.mZone != challenge.mZone)
							break;
						if (frame.mRings > challenge.mProgress)
						{
							challenge.mProgress = frame.mRings;
							gDirty = true;
							if (challenge.mProgress >= challenge.mTarget)
								beat(index);
						}
						break;

					case Type::SONIC_STEPS:
					{
						gStepDistance += frame.mGroundSubpixels;
						const uint32_t stepLength = n3ds::journal::GAME_STEP_PIXELS * 256;
						if (gStepDistance >= stepLength)
						{
							challenge.mProgress += gStepDistance / stepLength;
							gStepDistance %= stepLength;
							gDirty = true;
							if (challenge.mProgress >= challenge.mTarget)
								beat(index);
						}
						break;
					}

					default:
						break;
				}
			}

			// Progress gets saved once a minute (beaten challenges right away)
			if (gDirty)
			{
				if (gNextSave == 0)
					gNextSave = now + 60 * (uint64_t)SYSCLOCK_ARM11;
				else if (now >= gNextSave)
					save();
			}
			return (int)gData.mBeaten != beatenBefore;
		}

		bool onActCompleted(uint8_t zone, uint8_t act, uint16_t rings, uint32_t seconds, bool wasHit)
		{
			load();
			checkDay();
			const uint32_t beatenBefore = gData.mBeaten;
			for (int index = 0; index < NUM_PER_DAY; ++index)
			{
				Challenge& challenge = gData.mChallenges[index];
				if (challenge.mDone || zone != challenge.mZone)
					continue;
				switch (challenge.mType)
				{
					case Type::FINISH_ZONE:
						beat(index);
						break;

					case Type::ACT_UNDER_TIME:
						if (act != challenge.mAct)
							break;
						if (challenge.mProgress == 0 || seconds < challenge.mProgress)
						{
							challenge.mProgress = seconds;
							gDirty = true;
						}
						if (seconds < challenge.mTarget)
							beat(index);
						break;

					case Type::FINISH_WITH_RINGS:
						if (rings >= challenge.mTarget)
							beat(index);
						break;

					case Type::NO_HIT_ACT:
						if (!wasHit)
							beat(index);
						break;

					default:
						break;
				}
			}
			if (gDirty)
				save();
			return gData.mBeaten != beatenBefore;
		}

		int takeBeatenNotice()
		{
			const int notice = gBeatenNotice;
			gBeatenNotice = -1;
			return notice;
		}

		bool takeLevelUpNotice()
		{
			const bool notice = gLevelUpNotice;
			gLevelUpNotice = false;
			return notice;
		}

		std::string describe(const Challenge& challenge)
		{
			char text[96];
			const char* zone = getZoneName(challenge.mZone);
			switch (challenge.mType)
			{
				case Type::RINGS_IN_ZONE:		snprintf(text, sizeof(text), "Collect %u rings in %s", (unsigned)challenge.mTarget, zone);  break;
				case Type::FINISH_ZONE:			snprintf(text, sizeof(text), "Finish an act of %s", zone);  break;
				case Type::SONIC_STEPS:			snprintf(text, sizeof(text), "Run %u steps", (unsigned)challenge.mTarget);  break;
				case Type::RINGS_AT_ONCE:		snprintf(text, sizeof(text), "Hold %u rings at once", (unsigned)challenge.mTarget);  break;
				case Type::FINISH_WITH_RINGS:	snprintf(text, sizeof(text), "Finish %s with %u rings", zone, (unsigned)challenge.mTarget);  break;
				case Type::NO_HIT_ACT:			snprintf(text, sizeof(text), "Clear %s without a hit", zone);  break;
				case Type::ACT_UNDER_TIME:
				{
					const bool oneAct = (challenge.mZone < NUM_ZONES && ZONES[challenge.mZone].mActs < 2);
					char act[8] = { 0 };
					if (!oneAct)
						snprintf(act, sizeof(act), " %d", challenge.mAct + 1);
					snprintf(text, sizeof(text), "%s%s in under %u:%02u", zone, act, (unsigned)(challenge.mTarget / 60), (unsigned)(challenge.mTarget % 60));
					break;
				}
				default:
					return "?";
			}
			return text;
		}

		std::string describeProgress(const Challenge& challenge)
		{
			char text[48] = { 0 };
			switch (challenge.mType)
			{
				case Type::RINGS_IN_ZONE:
				case Type::RINGS_AT_ONCE:
					snprintf(text, sizeof(text), "%u / %u rings", (unsigned)std::min(challenge.mProgress, challenge.mTarget), (unsigned)challenge.mTarget);
					break;
				case Type::SONIC_STEPS:
					snprintf(text, sizeof(text), "%u / %u steps", (unsigned)std::min(challenge.mProgress, challenge.mTarget), (unsigned)challenge.mTarget);
					break;
				case Type::ACT_UNDER_TIME:
					if (challenge.mProgress > 0)
						snprintf(text, sizeof(text), "Best: %u:%02u", (unsigned)(challenge.mProgress / 60), (unsigned)(challenge.mProgress % 60));
					break;
				default:
					break;
			}
			return text;
		}

		const char* getTaunt(int index, bool beaten, uint32_t date)
		{
			static const char* const BEATEN[] =
			{
				"Grr! Beginner's luck, hedgehog!",
				"Impossible! I'll have a harder one tomorrow!",
				"This isn't over! Not by a long shot!",
				"Curses! Ruined again!",
				"Enjoy it while it lasts. Hohoho... hmph.",
			};
			if (beaten)
				return BEATEN[(date + (uint32_t)index * 2) % (sizeof(BEATEN) / sizeof(BEATEN[0]))];

			const Data& data = gData;
			if (index < 0 || index >= NUM_PER_DAY)
				return "";
			switch (data.mChallenges[index].mType)
			{
				case Type::RINGS_IN_ZONE:		return "Those rings are MINE! Come and take them, if you can!";
				case Type::FINISH_ZONE:			return "My badniks are waiting for you there. Hohoho!";
				case Type::SONIC_STEPS:			return "All that running... you'll be far too tired to stop me!";
				case Type::RINGS_AT_ONCE:		return "That many rings? You'll drop them all at the first spike!";
				case Type::ACT_UNDER_TIME:		return "Even my Egg Mobile is faster than you!";
				case Type::FINISH_WITH_RINGS:	return "Keep your rings all the way to the end? Never!";
				case Type::NO_HIT_ACT:			return "Not a single scratch? Hohoho! I'd like to see you try!";
				default:						return "";
			}
		}

		const char* getZoneName(uint8_t zone)
		{
			return (zone < NUM_ZONES) ? ZONES[zone].mName : "Somewhere";
		}

		int getXpFor(int index)
		{
			return 10 * (index + 1);
		}

		int getLevel(uint32_t xp)
		{
			return (int)(xp / XP_PER_LEVEL) + 1;
		}

		const char* getRank(int level)
		{
			static const char* const RANKS[] = { "Rookie", "Ring Collector", "Badnik Buster", "Egg Mobile Dodger", "Robotnik's Rival",
												 "Egg Breaker", "Death Egg Defier", "Robotnik's Nightmare" };
			const int count = (int)(sizeof(RANKS) / sizeof(RANKS[0]));
			return RANKS[std::min(std::max(level, 1), count) - 1];
		}
	}
}

#endif
