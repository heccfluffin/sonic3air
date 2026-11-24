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

			// What happened in the act being played (for challenges within one act)
			uint16_t gTrackedAct = 0xffff;
			uint32_t gActStartScore = 0;
			uint8_t gActShields = 0;
			int gLastLives = -1;

			uint32_t gEmeraldsSeen = 0;		// The most emeralds seen in a game (for "Go Super")
			uint32_t (*gMovesProvider)() = nullptr;

			// The acts for "Outrun your shadow": long stretches to keep running, without much waiting for platforms or
			// elevators (AIZ 2, MGZ 1, FBZ 1, MHZ 1 + 2); not AIZ 1, it never ends with a results screen (it burns into act 2)
			const uint8_t SHADOW_ACTS[][2] = { { 0, 1 }, { 2, 0 }, { 4, 0 }, { 7, 0 }, { 7, 1 } };

			const char* const MOVE_NAMES[(int)Move::_NUM] = { "spindashes", "drop dashes", "Super Peel-Outs", "insta-shields", "glides" };

			inline Move getMove(const Challenge& challenge)		{ return (Move)std::min<uint32_t>(challenge.mTarget / MOVE_FACTOR, (uint32_t)Move::_NUM - 1); }
			inline uint32_t getMoveCount(const Challenge& challenge)	{ return challenge.mTarget % MOVE_FACTOR; }

			uint32_t today()
			{
				const n3ds::profile::LocalTime now = n3ds::profile::getLocalTime();
				return (uint32_t)(now.mYear * 10000 + now.mMonth * 100 + now.mDay);
			}

			std::string toText(const Data& data)
			{
				char text[160];
				snprintf(text, sizeof(text), "date=%u\nxp=%u\nbeaten=%u\nperfect_days=%u\nzones=%u\nemeralds=%u\npinned=%d\n",
						 (unsigned)data.mDate, (unsigned)data.mXp, (unsigned)data.mBeaten, (unsigned)data.mPerfectDays, (unsigned)gZoneMask, (unsigned)gEmeraldsSeen, data.mPinned);
				std::string result = text;
				for (int index = 0; index < data.mCount; ++index)
				{
					const Challenge& challenge = data.mChallenges[index];
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
					// Angel Island 1 never counts as finished (no results screen, it burns into act 2), so it's always act 2 there
					const uint32_t finished = (gZoneMask >> (zone * 2)) & 3;
					if (zone == 0)
						return 1;
					if (ZONES[zone].mActs < 2 || finished == 1)
						return 0;
					if (finished == 2)
						return 1;
					return (uint8_t)next(2);
				};

				// A move that can be used: the spindash always, the others only if available
				const uint32_t availableMoves = ((nullptr != gMovesProvider) ? gMovesProvider() : 0) | (1u << (int)Move::SPINDASH);
				const auto pickMove = [&](bool medium) -> uint32_t
				{
					std::vector<int> moves;
					for (int move = 0; move < (int)Move::_NUM; ++move)
					{
						if (availableMoves & (1u << move))
							moves.push_back(move);
					}
					static const uint32_t EASY_COUNTS[(int)Move::_NUM]   = { 10, 5, 5, 10, 5 };
					static const uint32_t MEDIUM_COUNTS[(int)Move::_NUM] = { 25, 10, 10, 20, 15 };
					const int move = moves[next((uint32_t)moves.size())];
					return (uint32_t)move * MOVE_FACTOR + (medium ? MEDIUM_COUNTS[move] : EASY_COUNTS[move]);
				};
				const auto pickShadowAct = [&](Challenge& challenge)
				{
					std::vector<int> candidates;
					for (int k = 0; k < (int)(sizeof(SHADOW_ACTS) / sizeof(SHADOW_ACTS[0])); ++k)
					{
						if (isZoneAvailable(SHADOW_ACTS[k][0]))
							candidates.push_back(k);
					}
					const int pick = candidates[next((uint32_t)candidates.size())];		// Never empty: Angel Island is always there
					challenge.mType = Type::SHADOW_RUN;
					challenge.mZone = SHADOW_ACTS[pick][0];
					challenge.mAct = SHADOW_ACTS[pick][1];
				};

				Data& data = gData;
				data.mDate = date;
				data.mPinned = -1;
				data.mCount = getSlotsForLevel(getLevel(data.mXp));
				for (int index = 0; index < data.mCount; ++index)
				{
					Challenge challenge;
					const uint32_t kind = next((index == 4) ? 4 : 3);
					if (index >= 3)
					{
						// Expert (40 XP) and Robotnik's Special (50 XP): both at once, or a lot of rings; or a shadow that's the end
						const bool special = (index == 4);
						if (kind == 3)
						{
							pickShadowAct(challenge);
							challenge.mTarget = 1;
						}
						else if (kind == 0)
						{
							challenge.mType = Type::FAST_NO_HIT;
							challenge.mZone = pickZone();
							challenge.mAct = pickAct(challenge.mZone);
							const uint32_t par = ZONES[challenge.mZone].mParTime[challenge.mAct];
							challenge.mTarget = special ? (par * 3 / 4 / 10 * 10) : par;
						}
						else if (kind == 1)
						{
							challenge.mType = Type::FINISH_WITH_RINGS;
							challenge.mZone = pickZone();
							challenge.mTarget = special ? 100 : 75;		// Never more than 100 rings: one hit loses them all
						}
						else
						{
							// A really fast run: two thirds of the relaxed time, or three fifths for the special
							challenge.mType = Type::ACT_UNDER_TIME;
							challenge.mZone = pickZone();
							challenge.mAct = pickAct(challenge.mZone);
							const uint32_t par = ZONES[challenge.mZone].mParTime[challenge.mAct];
							challenge.mTarget = (special ? (par * 3 / 5) : (par * 2 / 3)) / 10 * 10;
						}
					}
					else if (index == 0)
					{
						// Easy
						const uint32_t easyKind = next(6);
						if (easyKind == 4)
						{
							challenge.mType = Type::MONITORS;
							challenge.mTarget = 5;
						}
						else if (easyKind == 5)
						{
							challenge.mType = Type::MOVES;
							challenge.mTarget = pickMove(false);
						}
						else if (easyKind == 3)
						{
							challenge.mType = Type::BADNIKS;
							challenge.mTarget = 10;
						}
						else if (easyKind == 0)
						{
							challenge.mType = Type::RINGS_IN_ZONE;
							challenge.mZone = pickZone();
							challenge.mTarget = (next(2) == 0) ? 30 : 50;
						}
						else if (easyKind == 1)
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
						const uint32_t mediumKind = next(11);
						if (mediumKind == 8)
						{
							challenge.mType = Type::MONITORS;
							challenge.mTarget = 15;
						}
						else if (mediumKind == 9 && gEmeraldsSeen >= 7)
						{
							challenge.mType = Type::GO_SUPER;		// Only with all the emeralds, and then 50 rings and a jump
						}
						else if (mediumKind == 10 && isZoneAvailable(3))
						{
							challenge.mType = Type::BALLOONS;
							challenge.mZone = 3;
							challenge.mTarget = 10;
						}
						else if (mediumKind >= 9)
						{
							challenge.mType = Type::MOVES;
							challenge.mTarget = pickMove(true);
						}
						else if (mediumKind >= 3)
						{
							switch (mediumKind)
							{
								case 3:  challenge.mType = Type::BADNIKS;  challenge.mTarget = 25;  break;
								case 4:  challenge.mType = Type::SCORE_IN_ACT;  challenge.mTarget = 10000;  break;		// About a good act: 3 minutes, 50 rings, some badniks
								case 5:  challenge.mType = Type::EXTRA_LIFE;  challenge.mZone = pickZone();  break;
								case 6:  challenge.mType = Type::FINISH_AS;  challenge.mZone = pickZone();  challenge.mTarget = (next(2) == 0) ? 2 : 3;  break;
								default: challenge.mType = Type::BONUS_STAGE;  break;
							}
						}
						else if (mediumKind == 0)
						{
							challenge.mType = Type::RINGS_AT_ONCE;
							challenge.mTarget = 100;
						}
						else if (mediumKind == 1)
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
						const uint32_t hardKind = next(8);
						if (hardKind == 6 || (hardKind == 7 && !isZoneAvailable(8)))
						{
							pickShadowAct(challenge);
						}
						else if (hardKind == 7)
						{
							// Sandopolis 2 with the lights kept on: hit the light switches before it gets dark enough for the ghosts
							challenge.mType = Type::NO_GHOSTS;
							challenge.mZone = 8;
							challenge.mAct = 1;
						}
						else if (hardKind == 5)
						{
							challenge.mType = Type::SCORE_IN_ACT;		// Like under 2 minutes with 100 rings (time bonus 10,000 plus ring bonus 10,000)
							challenge.mTarget = 20000;
						}
						else if (hardKind == 3)
						{
							// All three elemental shields in one act: only acts with at least two monitors of each (counted in the level
							// object lists), so it never hangs on a single monitor that might be on another character's path
							static const uint8_t SHIELD_ACTS[][2] = { { 0, 1 }, { 3, 0 }, { 3, 1 }, { 6, 0 }, { 7, 1 }, { 8, 1 } };	// AIZ 2, CNZ 1 + 2, LBZ 1, MHZ 2, SOZ 2
							std::vector<int> candidates;
							for (int k = 0; k < (int)(sizeof(SHIELD_ACTS) / sizeof(SHIELD_ACTS[0])); ++k)
							{
								if (isZoneAvailable(SHIELD_ACTS[k][0]))
									candidates.push_back(k);
							}
							if (candidates.empty())
							{
								challenge.mType = Type::NO_HIT_ACT;
								challenge.mZone = pickZone();
							}
							else
							{
								const int pick = candidates[next((uint32_t)candidates.size())];
								challenge.mType = Type::ALL_SHIELDS;
								challenge.mZone = SHIELD_ACTS[pick][0];
								challenge.mAct = SHIELD_ACTS[pick][1];
							}
						}
						else if (hardKind == 4)
						{
							// Hydrocity without running low on air, once it's been reached; otherwise a run without a hit
							challenge.mType = isZoneAvailable(1) ? Type::NO_DROWN : Type::NO_HIT_ACT;
							challenge.mZone = isZoneAvailable(1) ? 1 : pickZone();
						}
						else if (hardKind == 0)
						{
							challenge.mType = Type::NO_HIT_ACT;
							challenge.mZone = pickZone();
						}
						else if (hardKind == 1)
						{
							challenge.mType = Type::RINGS_IN_ZONE;		// 100 rings, but in one particular zone
							challenge.mZone = pickZone();
							challenge.mTarget = 100;
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
				std::string list;
				for (int index = 0; index < data.mCount; ++index)
					list += ((index > 0) ? " / " : "") + describe(data.mChallenges[index]);
				RMX_LOG_INFO("Daily challenge for " << date << ": " << list);
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
						else if (sscanf(line, "emeralds=%u", &value) == 1)		gEmeraldsSeen = value;
						else if (sscanf(line, "pinned=%d", &type) == 1)			gData.mPinned = type;
						else if (sscanf(line, "challenge=%d %d %d %u %u %d", &type, &zone, &act, &target, &progress, &done) == 6 && index < MAX_PER_DAY &&
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
					gData.mCount = index;
					if (gData.mPinned >= index || (gData.mPinned >= 0 && gData.mChallenges[gData.mPinned].mDone))
						gData.mPinned = -1;
					if (index < 3)
					{
						gData.mDate = 0;		// Incomplete: new challenges
						gData.mCount = 3;
					}
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
				if (gData.mPinned == index)
					gData.mPinned = -1;
				const int levelBefore = getLevel(gData.mXp);
				gData.mXp += (uint32_t)getXpFor(index);
				++gData.mBeaten;
				bool allDone = true;
				for (int other = 0; other < gData.mCount; ++other)
					allDone = allDone && gData.mChallenges[other].mDone;
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

		void setAvailableMovesProvider(uint32_t (*provider)())
		{
			gMovesProvider = provider;
		}

		bool isShadowAct(uint8_t zone, uint8_t act, bool& deadly)
		{
			// If both the hard and the special one are here, the special one counts (outrunning it beats both)
			load();
			bool found = false;
			deadly = false;
			for (int index = 0; index < gData.mCount; ++index)
			{
				const Challenge& challenge = gData.mChallenges[index];
				if (challenge.mType == Type::SHADOW_RUN && !challenge.mDone && challenge.mZone == zone && challenge.mAct == act)
				{
					found = true;
					deadly = deadly || (challenge.mTarget == 1);
				}
			}
			return found;
		}

		void setPinned(int index)
		{
			load();
			if (index >= 0 && (index >= gData.mCount || gData.mChallenges[index].mDone))
				index = -1;
			gData.mPinned = index;
			save();
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

			// A new act (or a new game, with a lower score): start counting for it
			const uint16_t act = (uint16_t)((frame.mZone << 8) | frame.mAct);
			if (act != gTrackedAct || frame.mScore < gActStartScore)
			{
				gTrackedAct = act;
				gActStartScore = frame.mScore;
				gActShields = 0;
			}
			gActShields |= (frame.mShields & 0x70);
			if (frame.mEmeralds > gEmeraldsSeen && frame.mEmeralds <= 7)
			{
				gEmeraldsSeen = frame.mEmeralds;
				gDirty = true;
			}
			const bool extraLife = (gLastLives >= 0 && frame.mLives > gLastLives);
			gLastLives = frame.mLives;

			for (int index = 0; index < gData.mCount; ++index)
			{
				Challenge& challenge = gData.mChallenges[index];
				if (challenge.mDone)
					continue;
				switch (challenge.mType)
				{
					case Type::BADNIKS:
						if (frame.mBadniksDestroyed > 0)
						{
							challenge.mProgress += frame.mBadniksDestroyed;
							gDirty = true;
							if (challenge.mProgress >= challenge.mTarget)
								beat(index);
						}
						break;

					case Type::EXTRA_LIFE:
						if (extraLife && frame.mZone == challenge.mZone)
							beat(index);
						break;

					case Type::MONITORS:
					case Type::BALLOONS:
					case Type::MOVES:
					{
						uint32_t count = 0;
						uint32_t target = challenge.mTarget;
						if (challenge.mType == Type::MONITORS)
							count = frame.mMonitorsBroken;
						else if (challenge.mType == Type::BALLOONS)
							count = (frame.mZone == challenge.mZone) ? frame.mBalloonsPopped : 0;
						else
						{
							count = frame.mMovesUsed[(int)getMove(challenge)];
							target = getMoveCount(challenge);
						}
						if (count > 0)
						{
							challenge.mProgress += count;
							gDirty = true;
							if (challenge.mProgress >= target)
								beat(index);
						}
						break;
					}

					case Type::GO_SUPER:
						if (frame.mSuper)
							beat(index);
						break;

					case Type::BONUS_STAGE:
						if (frame.mZone >= 0x13 && frame.mZone <= 0x15)
							beat(index);
						break;

					case Type::SCORE_IN_ACT:
					{
						const uint32_t scored = frame.mScore - gActStartScore;
						if (scored > challenge.mProgress)
						{
							challenge.mProgress = scored;
							gDirty = true;
							if (scored >= challenge.mTarget)
								beat(index);
						}
						break;
					}

					case Type::ALL_SHIELDS:
					{
						if (frame.mZone != challenge.mZone || frame.mAct != challenge.mAct)
							break;
						const uint32_t count = ((gActShields & 0x10) ? 1 : 0) + ((gActShields & 0x20) ? 1 : 0) + ((gActShields & 0x40) ? 1 : 0);
						if (count > challenge.mProgress)
						{
							challenge.mProgress = count;
							gDirty = true;
							if (count >= 3)
								beat(index);
						}
						break;
					}

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

		bool onActCompleted(const ActResult& result)
		{
			const uint8_t zone = result.mZone;
			const uint8_t act = result.mAct;
			const uint16_t rings = result.mRings;
			const uint32_t seconds = result.mSeconds;
			const bool wasHit = result.mWasHit;
			load();
			checkDay();
			const uint32_t beatenBefore = gData.mBeaten;
			for (int index = 0; index < gData.mCount; ++index)
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

					case Type::FINISH_AS:
						if ((challenge.mTarget == 2 && result.mCharacters == 2) || (challenge.mTarget == 3 && (result.mCharacters == 3 || result.mCharacters == 4)))
							beat(index);
						break;

					case Type::NO_DROWN:
						if (!result.mLowOnAir)
							beat(index);
						break;

					case Type::SHADOW_RUN:
						if (act == challenge.mAct && !result.mShadowTouched)
							beat(index);
						break;

					case Type::NO_GHOSTS:
						if (act == challenge.mAct && !result.mGhostsWoken)
							beat(index);
						break;

					case Type::FAST_NO_HIT:
						if (act != challenge.mAct)
							break;
						if (!wasHit && (challenge.mProgress == 0 || seconds < challenge.mProgress))
						{
							challenge.mProgress = seconds;		// Best time without a hit
							gDirty = true;
						}
						if (!wasHit && seconds < challenge.mTarget)
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
				case Type::BADNIKS:				snprintf(text, sizeof(text), "Destroy %u badniks", (unsigned)challenge.mTarget);  break;
				case Type::EXTRA_LIFE:			snprintf(text, sizeof(text), "Earn an extra life in %s", zone);  break;
				case Type::BONUS_STAGE:			snprintf(text, sizeof(text), "Find a bonus stage");  break;
				case Type::SCORE_IN_ACT:
					if (challenge.mTarget >= 1000)
						snprintf(text, sizeof(text), "Score %u,%03u points in one act", (unsigned)(challenge.mTarget / 1000), (unsigned)(challenge.mTarget % 1000));
					else
						snprintf(text, sizeof(text), "Score %u points in one act", (unsigned)challenge.mTarget);
					break;
				case Type::FINISH_AS:			snprintf(text, sizeof(text), "Finish %s as %s", zone, (challenge.mTarget == 2) ? "Tails" : "Knuckles");  break;
				case Type::ALL_SHIELDS:			snprintf(text, sizeof(text), "All 3 elemental shields in %s %d", zone, challenge.mAct + 1);  break;
				case Type::NO_DROWN:			snprintf(text, sizeof(text), "Clear %s, never low on air", zone);  break;
				case Type::SHADOW_RUN:			snprintf(text, sizeof(text), "%s your shadow in %s %d", (challenge.mTarget == 1) ? "Escape" : "Outrun", zone, challenge.mAct + 1);  break;
				case Type::MONITORS:			snprintf(text, sizeof(text), "Break %u monitors", (unsigned)challenge.mTarget);  break;
				case Type::GO_SUPER:			snprintf(text, sizeof(text), "Go Super");  break;
				case Type::BALLOONS:			snprintf(text, sizeof(text), "Pop %u balloons in %s", (unsigned)challenge.mTarget, zone);  break;
				case Type::NO_GHOSTS:			snprintf(text, sizeof(text), "Clear %s %d without a ghost", zone, challenge.mAct + 1);  break;
				case Type::MOVES:
				{
					const Move move = getMove(challenge);
					const unsigned count = (unsigned)getMoveCount(challenge);
					if (move == Move::INSTA_SHIELD)
						snprintf(text, sizeof(text), "Use the insta-shield %u times", count);
					else if (move == Move::GLIDE)
						snprintf(text, sizeof(text), "Glide %u times as Knuckles", count);
					else
						snprintf(text, sizeof(text), "Do %u %s", count, MOVE_NAMES[(int)move]);
					break;
				}
				case Type::ACT_UNDER_TIME:
				case Type::FAST_NO_HIT:
				{
					const bool oneAct = (challenge.mZone < NUM_ZONES && ZONES[challenge.mZone].mActs < 2);
					char act[8] = { 0 };
					if (!oneAct)
						snprintf(act, sizeof(act), " %d", challenge.mAct + 1);
					snprintf(text, sizeof(text), "%s%s in under %u:%02u%s", zone, act, (unsigned)(challenge.mTarget / 60), (unsigned)(challenge.mTarget % 60),
							 (challenge.mType == Type::FAST_NO_HIT) ? " without a hit" : "");
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
				case Type::BADNIKS:
					snprintf(text, sizeof(text), "%u / %u badniks", (unsigned)std::min(challenge.mProgress, challenge.mTarget), (unsigned)challenge.mTarget);
					break;
				case Type::MONITORS:
					snprintf(text, sizeof(text), "%u / %u monitors", (unsigned)std::min(challenge.mProgress, challenge.mTarget), (unsigned)challenge.mTarget);
					break;
				case Type::BALLOONS:
					snprintf(text, sizeof(text), "%u / %u balloons", (unsigned)std::min(challenge.mProgress, challenge.mTarget), (unsigned)challenge.mTarget);
					break;
				case Type::MOVES:
					snprintf(text, sizeof(text), "%u / %u", (unsigned)std::min(challenge.mProgress, getMoveCount(challenge)), (unsigned)getMoveCount(challenge));
					break;
				case Type::SCORE_IN_ACT:
					if (challenge.mProgress > 0)
						snprintf(text, sizeof(text), "Best: %u points", (unsigned)challenge.mProgress);
					break;
				case Type::ALL_SHIELDS:
					if (challenge.mProgress > 0)
						snprintf(text, sizeof(text), "Best: %u / 3 shields", (unsigned)challenge.mProgress);
					break;
				case Type::ACT_UNDER_TIME:
				case Type::FAST_NO_HIT:
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
			{
				if (index >= 0 && index < gData.mCount && gData.mChallenges[index].mType == Type::BADNIKS)
					return "My poor badniks! Do you know how long those take to build?!";
				if (index >= 0 && index < gData.mCount && gData.mChallenges[index].mType == Type::SHADOW_RUN)
					return "Outran by the original?! My shadow needs more practice!";
				return BEATEN[(date + (uint32_t)index * 2) % (sizeof(BEATEN) / sizeof(BEATEN[0]))];
			}

			const Data& data = gData;
			if (index < 0 || index >= data.mCount)
				return "";
			if (index == 4 && data.mChallenges[index].mType != Type::FAST_NO_HIT && data.mChallenges[index].mType != Type::SHADOW_RUN)
				return "Robotnik's Special! My finest work. You don't stand a chance!";
			switch (data.mChallenges[index].mType)
			{
				case Type::RINGS_IN_ZONE:		return "Those rings are MINE! Come and take them, if you can!";
				case Type::FINISH_ZONE:			return "My badniks are waiting for you there. Hohoho!";
				case Type::SONIC_STEPS:			return "All that running... you'll be far too tired to stop me!";
				case Type::RINGS_AT_ONCE:		return "That many rings? You'll drop them all at the first spike!";
				case Type::ACT_UNDER_TIME:		return "Even my Egg Mobile is faster than you!";
				case Type::FINISH_WITH_RINGS:	return "Keep your rings all the way to the end? Never!";
				case Type::NO_HIT_ACT:			return "Not a single scratch? Hohoho! I'd like to see you try!";
				case Type::BADNIKS:				return "My badniks will flatten you long before that! Hohoho!";
				case Type::EXTRA_LIFE:			return "An extra life? You'll need a lot more than one against me!";
				case Type::BONUS_STAGE:			return "My bonus stages are rigged, you know. Not that you'll find one!";
				case Type::SCORE_IN_ACT:		return "Points! Only I am allowed to score points around here!";
				case Type::FINISH_AS:			return "Sending in your friends? They're no better than you, hedgehog!";
				case Type::ALL_SHIELDS:			return "Fire, lightning AND bubbles? Greedy little pest!";
				case Type::NO_DROWN:			return "I timed every air bubble in Hydrocity myself. Glub glub! Hohoho!";
				case Type::SHADOW_RUN:			return (data.mChallenges[index].mTarget == 1) ? "My shadow never stops, hedgehog. Stand still and it's over!"
																							  : "Meet my newest creation: YOU! Stop for a moment and... Hohoho!";
				case Type::MONITORS:			return "Hands off my monitors! Those are company property!";
				case Type::MOVES:				return "Show-off! All those fancy moves won't save you!";
				case Type::GO_SUPER:			return "Super, are you? Enjoy it while your rings last. Hohoho!";
				case Type::BALLOONS:			return "Those balloons are for MY carnival! Don't you dare pop them!";
				case Type::NO_GHOSTS:			return "The ghosts of Sandopolis love the dark. Sweet dreams! Hohoho!";
				case Type::FAST_NO_HIT:			return (index == 4) ? "My very own special! Fast AND flawless. Nobody can do that! Nobody!"
																	: "Fast AND without a scratch? You'll trip over your own shoes!";
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

		const char* getSlotName(int index)
		{
			static const char* const NAMES[MAX_PER_DAY] = { "Easy", "Medium", "Hard", "Expert", "Robotnik's Special" };
			return (index >= 0 && index < MAX_PER_DAY) ? NAMES[index] : "";
		}

		int getSlotsForLevel(int level)
		{
			return (level >= SPECIAL_LEVEL) ? 5 : (level >= EXPERT_LEVEL) ? 4 : 3;
		}

		int getLevel(uint32_t xp)
		{
			int level = 1;
			while (xp >= getXpForNextLevel(level))
			{
				xp -= getXpForNextLevel(level);
				++level;
			}
			return level;
		}

		uint32_t getXpForNextLevel(int level)
		{
			return (uint32_t)std::min(40 + 10 * std::max(level, 1), 150);
		}

		uint32_t getXpIntoLevel(uint32_t xp)
		{
			int level = 1;
			while (xp >= getXpForNextLevel(level))
			{
				xp -= getXpForNextLevel(level);
				++level;
			}
			return xp;
		}

		std::string getRank(int level)
		{
			// A new name every two levels, then the top one
			static const char* const RANKS[] =
			{
				"Rookie", "Ring Collector", "Badnik Buster", "Spring Jumper", "Egg Mobile Dodger", "Capsule Cracker", "Robotnik's Rival",
				"Chaos Chaser", "Egg Breaker", "Emerald Guardian", "Death Egg Defier", "Super Speedster", "Hyper Hero"
			};
			static const char* const TOP_RANK = "Robotnik's Nightmare";
			static_assert(sizeof(RANKS) / sizeof(RANKS[0]) * 2 == TOP_RANK_LEVEL, "A rank for every two levels below the top");
			level = std::max(level, 1);
			if (level < TOP_RANK_LEVEL)
				return RANKS[(level - 1) / 2];
			if (level == TOP_RANK_LEVEL)
				return TOP_RANK;

			// Past the top: the same name with a Roman numeral, II for the level after it, and so on (written out in full)
			int number = level - TOP_RANK_LEVEL + 1;
			static const struct { int mValue; const char* mText; } NUMERALS[] =
				{ { 1000, "M" }, { 900, "CM" }, { 500, "D" }, { 400, "CD" }, { 100, "C" }, { 90, "XC" }, { 50, "L" }, { 40, "XL" }, { 10, "X" }, { 9, "IX" }, { 5, "V" }, { 4, "IV" }, { 1, "I" } };
			std::string numeral;
			for (const auto& entry : NUMERALS)
			{
				while (number >= entry.mValue && numeral.length() < 24)
				{
					numeral += entry.mText;
					number -= entry.mValue;
				}
			}
			return std::string(TOP_RANK) + " " + numeral;
		}

		Metal getMetal(int level)
		{
			return (level <= 4) ? Metal::BRONZE : (level <= 9) ? Metal::SILVER : (level <= 14) ? Metal::GOLD :
				   (level <= 19) ? Metal::DIAMOND : (level < TOP_RANK_LEVEL) ? Metal::PLATINUM : Metal::EMERALD;
		}
	}
}

#endif
