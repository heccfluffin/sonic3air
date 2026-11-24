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
		static const int MAX_PER_DAY = 5;			// Easy, medium, hard; expert from medal level 10, Robotnik's Special from level 20
		static const int EXPERT_LEVEL = 10;
		static const int SPECIAL_LEVEL = 20;


		enum class Type : uint8_t
		{
			RINGS_IN_ZONE = 0,		// Have "mTarget" rings at once in the zone (easy, hard)
			FINISH_ZONE,			// Finish any act of the zone (easy)
			SONIC_STEPS,			// Run "mTarget" of Sonic's steps today (easy)
			RINGS_AT_ONCE,			// Have "mTarget" rings at once, anywhere (medium); never more than 100
			ACT_UNDER_TIME,			// Finish the zone's act in less than "mTarget" seconds (medium, hard, expert, special)
			FINISH_WITH_RINGS,		// Finish an act of the zone with at least "mTarget" rings (medium, expert, special)
			NO_HIT_ACT,				// Finish an act of the zone without getting hit (hard)
			FAST_NO_HIT,			// Finish the zone's act in less than "mTarget" seconds and without getting hit (expert, special)
			BADNIKS,				// Destroy "mTarget" badniks today (easy, medium)
			EXTRA_LIFE,				// Earn an extra life in the zone (medium)
			BONUS_STAGE,			// Get into a bonus stage: Gumball Machine, Glowing Spheres or Slot Machine (medium)
			SCORE_IN_ACT,			// Score "mTarget" points within one act, with the act's end bonuses (medium 10,000, hard 20,000)
			FINISH_AS,				// Finish an act of the zone as "mTarget": 2 = Tails, 3 = Knuckles (medium)
			ALL_SHIELDS,			// Have the fire, lightning and bubble shield, all in the zone's act "mAct" (hard; only acts that have them all)
			NO_DROWN,				// Finish an act of the zone without the drowning countdown ever starting (hard, Hydrocity)
			SHADOW_RUN,				// Finish the zone's act "mAct" without the shadow ever touching you (hard); "mTarget" 1: closer behind, and his
									// touch is the end (Robotnik's Special)
			MONITORS,				// Break "mTarget" monitors today (easy, medium)
			MOVES,					// Use a move "mTarget" times today: the count, plus the move (as in "Move") times MOVE_FACTOR (easy, medium)
			GO_SUPER,				// Turn Super (medium; only once all seven emeralds were seen in a game)
			BALLOONS,				// Pop "mTarget" balloons in Carnival Night (medium)
			NO_GHOSTS,				// Finish Sandopolis 2 without a ghost coming out of the dark (no longer chosen: far too hard)
			LIGHT_SWITCHES,			// Turn the lights back on "mTarget" times in Sandopolis 2, with its light switches (medium)
			_NUM
		};

		// The moves for "MOVES" challenges; only moves that can be used get chosen (see "setAvailableMovesProvider")
		enum class Move : uint8_t
		{
			SPINDASH = 0,		// Anyone
			DROP_DASH,			// Sonic, once unlocked and switched on
			PEEL_OUT,			// Sonic's Super Peel-Out, the same
			INSTA_SHIELD,		// Sonic, if switched on
			GLIDE,				// Knuckles
			_NUM
		};
		static const uint32_t MOVE_FACTOR = 10000;

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
			Challenge mChallenges[MAX_PER_DAY];
			int mCount = 3;						// Today's challenges: 3, more with higher medal levels
			int mPinned = -1;					// The one pinned to the main menu, -1 for none (unpinned once beaten, and with each new day)
			uint32_t mXp = 0;					// In total, the level follows from it
			uint32_t mBeaten = 0;				// Challenges beaten in total
			uint32_t mPerfectDays = 0;			// Days with all of them beaten
		};

		// The challenges of today: new ones get chosen the first time this is asked on a day, from the zones in "finishedZoneActs"
		// (the game's bitmask of finished acts, bit zone * 2 + act) - Angel Island is always one of them
		const Data& get(uint32_t finishedZoneActs);
		const Data& get();		// With the zones last given

		void save();		// Writes the file if something changed; also called at exit

		// Asked when new challenges get chosen: a bit (1 << Move) for each move that can be used right now (unlocked, switched on)
		void setAvailableMovesProvider(uint32_t (*provider)());

		// The shadow ambush: the chance (in percent) of him turning up in an act, 0 for never; 1 by default
		static const int AMBUSH_CHANCES[] = { 0, 1, 5, 25, 100 };
		int getAmbushChance();
		void setAmbushChance(int percent);
		void addAmbushSurvived();
		uint32_t getAmbushesSurvived();

		// Pins one of today's challenges to the main menu (-1: none); only one that isn't beaten yet
		void setPinned(int index);

		// What the game tells every frame while a level runs (in a normal game or Act Select)
		struct Frame
		{
			uint8_t mZone = 0;					// Also the bonus stages: 0x13 Gumball Machine, 0x14 Glowing Spheres, 0x15 Slot Machine
			uint8_t mAct = 0;
			uint16_t mRings = 0;
			uint32_t mGroundSubpixels = 0;		// Run on the ground this frame, in 1/256 pixels
			uint8_t mLives = 0;
			uint32_t mScore = 0;				// In points
			uint8_t mShields = 0;				// The character's elemental shields right now: 0x10 fire, 0x20 lightning, 0x40 bubble
			uint8_t mBadniksDestroyed = 0;		// This frame
			uint8_t mMonitorsBroken = 0;		// This frame
			uint8_t mBalloonsPopped = 0;		// This frame
			uint8_t mLightsTurnedOn = 0;		// This frame: Sandopolis 2's lights back on after getting darker
			uint8_t mMovesUsed[(int)Move::_NUM] = { 0 };	// This frame
			bool mSuper = false;				// The character is Super (or Hyper) right now
			uint8_t mEmeralds = 0;				// The most Chaos or Super Emeralds of the game played
		};
		bool onFrame(const Frame& frame);		// True if a challenge got beaten just now

		// An act was finished: its time, the rings then, and what happened in it
		struct ActResult
		{
			uint8_t mZone = 0;
			uint8_t mAct = 0;
			uint16_t mRings = 0;
			uint32_t mSeconds = 0;
			bool mWasHit = false;			// Got hurt or died in it
			bool mLowOnAir = false;			// The drowning countdown started in it
			uint8_t mCharacters = 0;		// As in the game: 0 Sonic & Tails, 1 Sonic, 2 Tails, 3 Knuckles, 4 Knuckles & Tails
			bool mShadowTouched = false;	// The shadow touched the character in it
			bool mGhostsWoken = false;		// Sandopolis 2: a ghost came out
		};
		bool onActCompleted(const ActResult& result);		// Same

		// Whether the shadow runs in this act: a "SHADOW_RUN" challenge for it isn't beaten yet ("deadly" for the Robotnik's
		// Special version)
		bool isShadowAct(uint8_t zone, uint8_t act, bool& deadly);

		// A challenge was beaten since this was last asked (for the message in the 3DS menu); -1 if not
		int takeBeatenNotice();
		bool takeLevelUpNotice();

		// Texts
		std::string describe(const Challenge& challenge);			// Like "Collect 50 rings in Hydrocity"
		std::string describeProgress(const Challenge& challenge);	// Like "32 / 50", "Best: 3:12", or empty
		const char* getTaunt(int index, bool beaten, uint32_t date);	// Robotnik's words for it
		const char* getZoneName(uint8_t zone);
		int getXpFor(int index);		// 10, 20, 30, 40 or 50
		const char* getSlotName(int index);		// "Easy" to "Robotnik's Special"
		int getSlotsForLevel(int level);
		// Medal levels: the first takes 50 XP, each one after it 10 more, up to 150 (a perfect first day already brings level 2).
		// Each level gives a voucher.
		int getLevel(uint32_t xp);					// 1 and up
		uint32_t getXpForNextLevel(int level);		// XP from this level to the next
		uint32_t getXpIntoLevel(uint32_t xp);		// XP gathered toward the next level
		std::string getRank(int level);		// A name for every two levels up to the top, then "Robotnik's Nightmare II", "III" and so on, without end

		// The medal's metal: bronze, silver, gold, diamond, platinum, emerald
		enum class Metal : uint8_t { BRONZE = 0, SILVER, GOLD, DIAMOND, PLATINUM, EMERALD, _NUM };
		Metal getMetal(int level);
		static const int TOP_RANK_LEVEL = 26;		// Emerald, and "Robotnik's Nightmare"; every level after it adds a numeral (and more on the medal)
	}
}

#endif
