/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: Doctor Robotnik's Daily Challenge in the "3DS" menu - three new challenges every day (easy, medium,
*	hard), and a Robotnik medal that levels up with each one beaten. Stored in "savedata/3ds_challenges.txt".
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include <stdint.h>
#include <string>


namespace n3ds
{
	namespace challenges
	{
		static const int NUM_PER_DAY = 3;			// Easy, medium, hard
		static const int XP_PER_LEVEL = 100;		// Each medal level gives a voucher

		enum class Type : uint8_t
		{
			RINGS_IN_ZONE = 0,		// Have "mTarget" rings at once in the zone (easy)
			FINISH_ZONE,			// Finish any act of the zone (easy)
			SONIC_STEPS,			// Run "mTarget" of Sonic's steps today (easy)
			RINGS_AT_ONCE,			// Have "mTarget" rings at once, anywhere (medium / hard)
			ACT_UNDER_TIME,			// Finish the zone's act in less than "mTarget" seconds (medium / hard)
			FINISH_WITH_RINGS,		// Finish an act of the zone with at least "mTarget" rings (medium)
			NO_HIT_ACT,				// Finish an act of the zone without getting hit (hard)
			_NUM
		};

		struct Challenge
		{
			Type mType = Type::FINISH_ZONE;
			uint8_t mZone = 0;			// The game's zone number (0 = Angel Island, 1 = Hydrocity ...), if the challenge has one
			uint8_t mAct = 0;			// 0 or 1, for ACT_UNDER_TIME
			uint32_t mTarget = 0;		// Rings, steps or seconds
			uint32_t mProgress = 0;		// Best so far: rings, steps; for times the best time in seconds (0 = none yet)
			bool mDone = false;
		};

		struct Data
		{
			uint32_t mDate = 0;					// YYYYMMDD of the challenges
			Challenge mChallenges[NUM_PER_DAY];
			uint32_t mXp = 0;					// In total, the level follows from it
			uint32_t mBeaten = 0;				// Challenges beaten in total
			uint32_t mPerfectDays = 0;			// Days with all three beaten
		};

		// The challenges of today: new ones get chosen the first time this is asked on a day, from the zones in "finishedZoneActs"
		// (the game's bitmask of finished acts, bit zone * 2 + act) - Angel Island is always one of them
		const Data& get(uint32_t finishedZoneActs);
		const Data& get();		// With the zones last given

		void save();		// Writes the file if something changed; also called at exit

		// What the game tells every frame while a level runs (in a normal game or Act Select)
		struct Frame
		{
			uint8_t mZone = 0;
			uint8_t mAct = 0;
			uint16_t mRings = 0;
			uint32_t mGroundSubpixels = 0;		// Run on the ground this frame, in 1/256 pixels
		};
		bool onFrame(const Frame& frame);		// True if a challenge got beaten just now

		// An act was finished: its time in seconds, the rings then, and whether the character got hit in it
		bool onActCompleted(uint8_t zone, uint8_t act, uint16_t rings, uint32_t seconds, bool wasHit);		// Same

		// A challenge was beaten since this was last asked (for the message in the 3DS menu); -1 if not
		int takeBeatenNotice();
		bool takeLevelUpNotice();

		// Texts
		std::string describe(const Challenge& challenge);			// Like "Collect 50 rings in Hydrocity"
		std::string describeProgress(const Challenge& challenge);	// Like "32 / 50", "Best: 3:12", or empty
		const char* getTaunt(int index, bool beaten, uint32_t date);	// Robotnik's words for it
		const char* getZoneName(uint8_t zone);
		int getXpFor(int index);		// 10, 20 or 30
		int getLevel(uint32_t xp);		// 1 and up
		const char* getRank(int level);
	}
}

#endif
