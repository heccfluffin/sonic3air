/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the journal in the "3DS" menu - a stamp card for each day played, and a few numbers.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include <stdint.h>
#include <string>
#include <vector>


namespace n3ds
{
	namespace journal
	{
		static const int STAMPS_PER_CARD = 7;

		// Rewards for each completed card, besides the postcard
		static const int VOUCHER_PLAY_COINS = 10;		// A voucher: this many Play Coins off one purchase in the 3DS menu
		static const int BONUS_RING_COUNT = 50;

		// Sonic's steps in the game: one for every this many pixels run on the ground
		static const int GAME_STEP_PIXELS = 24;

		// What a bonus box holds, chosen when it's opened; given at the start of the next act in a normal game
		enum BonusGift
		{
			BONUS_NONE = 0,
			BONUS_CLASSIC_SHIELD,		// The shields use the same numbers as n3ds::shop::Shield
			BONUS_FIRE_SHIELD,
			BONUS_LIGHTNING_SHIELD,
			BONUS_BUBBLE_SHIELD,
			BONUS_RINGS,
			_NUM_BONUS_GIFTS
		};

		// A postcard for each completed stamp card: one of the game's zone preview pictures (sprites like "zone03_act1a"),
		// or a funny one with a picture from the game's achievements
		struct Postcard
		{
			int mFunny = -1;			// Index of a funny postcard, or -1 for a zone picture
			int mZone = 1;				// 1 to 14, as in the sprite names
			int mAct = 1;				// 1 or 2
			char mImage = 'a';			// Each act has two pictures, 'a' and 'b'
			uint32_t mDate = 0;			// YYYYMMDD, when it arrived

			std::string getSpriteName() const;		// Or "funny_<name>"
		};
		bool parsePostcardName(const std::string& spriteName, Postcard& outPostcard);		// The other way round; false if there's no such postcard

		// A postcard swapped with another player (by local wireless): a copy of one of theirs, with who sent it
		struct SwappedPostcard
		{
			Postcard mPostcard;			// mDate: when it arrived here
			std::string mFrom;			// Their console's user name
			uint32_t mFromCards = 0;	// Stamp cards they completed
			uint32_t mFromDays = 0;		// Days they played
		};

		int getNumPossiblePostcards();
		int getNumFunnyPostcards();
		const char* getFunnyPostcardName(int index);		// Like "decoys", nullptr if there's no such one

		struct Data
		{
			uint32_t mStartedDate = 0;		// YYYYMMDD, when the journal began
			uint32_t mLastDayPlayed = 0;	// YYYYMMDD
			uint32_t mDaysPlayed = 0;
			int mStamps = 0;				// On the current card, 0 to STAMPS_PER_CARD
			uint32_t mCardsCompleted = 0;
			uint64_t mPlaySeconds = 0;
			uint32_t mPlayCoinsSpent = 0;
			uint32_t mThingsBought = 0;
			uint32_t mScreenshots = 0;
			uint64_t mGameSteps = 0;		// Taken by the character in the game
			uint32_t mVouchers = 0;			// Not used yet
			uint32_t mBonusBoxes = 0;		// Not opened yet
			int mBonusReady = BONUS_NONE;	// From an opened box, waiting for the next act
			uint32_t mRewardedCards = 0;	// Completed cards that gave their voucher and bonus box
			bool mWelcomePostcard = false;	// The free one for opening the journal the first time was given
			std::vector<Postcard> mPostcards;		// In the order they arrived

			// Swapping postcards with other players
			std::vector<SwappedPostcard> mSwapped;	// In the order they arrived
			uint64_t mSwapId = 0;			// This journal's random ID, so a swap partner can tell who they swapped with
			int mShareIndex = -1;			// Which of the postcards in "mPostcards" goes to swap partners; -1 for the newest
			std::vector<std::pair<uint64_t, uint32_t>> mSwapPartners;	// Who was swapped with, and the day of it (the latest ones only)
		};

		const Data& get();		// Loaded on first use

		// Main thread, every frame: counts the play time, and gives the day's stamp the first time the game runs on a day.
		// Saves now and then (in the background), but only while a level is being played ("inLevel"): while the SD card writes
		// (about 2 seconds on a slow card), everything else reading from it waits, and menus like Mods read from it when opened.
		// Quitting saves anyway.
		void update(bool inLevel = true);

		// Writes the file if something changed; also called at exit
		void save();

		// Whether a stamp was collected since this was last asked (for a message in the 3DS menu)
		bool takeNewStampNotice();

		// Whether a postcard arrived since this was last asked (then the newest one in "mPostcards" is new)
		bool takeNewPostcardNotice();

		// A free postcard the first time the journal is opened; false if it was given before (or all postcards are collected)
		bool giveWelcomePostcard();

		// Whether vouchers or bonus boxes were given since this was last asked
		bool takeNewRewardNotice();

		// Uses one voucher, false if there's none
		bool useVoucher();

		// Vouchers from elsewhere (like Robotnik medal levels); shown with the next reward notice
		void addVouchers(int count);

		// A bonus box bought with Play Coins in the 3DS menu, waiting to be opened like the ones from stamp cards
		void addBonusBox();

		// Opens a bonus box with the given gift inside, ready for the next act; choosing again while one is ready only changes
		// what's inside, without another box. False if there's no box.
		bool prepareBonus(int gift);

		// The gift was given in the game
		void bonusGiven();

		// Swapping postcards: the one shared, whether a partner was already swapped with today, and a postcard that came in
		const Postcard* getSharedPostcard();		// nullptr if there's none yet
		void setShareIndex(int index);
		uint64_t getSwapId();
		bool hasSwappedToday(uint64_t partnerId);
		void addSwappedPostcard(const SwappedPostcard& swapped, uint64_t partnerId);
		bool takeNewSwapNotice();		// Whether a postcard came in by swapping since this was last asked

		// Counters
		void addPurchase(int playCoins);		// What was actually paid
		void addScreenshot();
		void addGameDistance(uint32_t subpixels);		// Run on the ground this frame, in 1/256 pixels

		std::string formatDate(uint32_t yyyymmdd);		// Like "21 Nov 2026"
	}
}

#endif
