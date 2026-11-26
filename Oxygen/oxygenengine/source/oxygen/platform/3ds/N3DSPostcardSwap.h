/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: swapping postcards with another player nearby, by local wireless (like Download Play - no Internet
*	needed). Both players open "Swap Postcards" in the 3DS menu's journal; the consoles take turns looking for each other's
*	little network and opening one of their own until they meet, send each other the postcard they share, and confirm it.
*	All the waiting happens in a thread of its own; the menu starts it, checks the status each frame, and stops it.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSJournal.h"

#include <cstdint>
#include <string>
#include <vector>


namespace n3ds
{
	namespace swap
	{
		// What one console sends the other
		struct Message
		{
			enum Type : uint8_t
			{
				CARD = 1,		// The shared postcard and who sends it
				THANKS = 2		// Got the other's card
			};
			Type mType = CARD;
			uint64_t mSenderId = 0;			// The sender journal's swap ID
			std::string mName;				// Their console's user name
			journal::Postcard mPostcard;
			uint32_t mCards = 0;			// Stamp cards completed
			uint32_t mDays = 0;				// Days played
		};
		static const size_t MESSAGE_SIZE = 64;

		std::vector<uint8_t> encode(const Message& message);
		bool decode(const uint8_t* data, size_t size, Message& outMessage);		// False if it isn't a valid message of this game

		enum class Phase
		{
			IDLE,
			SEARCHING,		// Looking for the other console (and letting it find this one)
			SWAPPING,		// Connected, sending and receiving
			DONE,			// See mSuccess and mMessage
		};

		struct Status
		{
			Phase mPhase = Phase::IDLE;
			bool mSuccess = false;
			std::string mMessage;			// What happened, for the player
			std::string mPartnerName;
			uint64_t mPartnerId = 0;
			journal::SwappedPostcard mReceived;		// With mSuccess
			int mSecondsLeft = 0;			// While searching
		};

		// Starts looking for a partner, with the postcard to share (from the journal); "playerName" is shown to them
		void start(const std::string& playerName);
		void stop();		// Cancels; waits for the thread to end (a moment)
		Status getStatus();
		bool isRunning();
	}
}

#endif
