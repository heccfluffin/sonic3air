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
			std::vector<Postcard> mPostcards;		// In the order they arrived
		};

		const Data& get();		// Loaded on first use

		// Main thread, every frame: counts the play time, and gives the day's stamp the first time the game runs on a day.
		// Saves now and then (in the background).
		void update();

		// Writes the file if something changed; also called at exit
		void save();

		// Whether a stamp was collected since this was last asked (for a message in the 3DS menu)
		bool takeNewStampNotice();

		// Whether a postcard arrived since this was last asked (then the newest one in "mPostcards" is new)
		bool takeNewPostcardNotice();

		// Counters
		void addPurchase(int playCoins);
		void addScreenshot();

		// The 3DS step counter: steps in total (false if they can't be read)
		bool getSteps(uint32_t& outTotal);

		std::string formatDate(uint32_t yyyymmdd);		// Like "21 Nov 2026"
	}
}

#endif
