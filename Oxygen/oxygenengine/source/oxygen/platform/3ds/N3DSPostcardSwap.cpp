/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: swapping postcards with another player nearby, by local wireless.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSPostcardSwap.h"

#include <3ds.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>


namespace n3ds
{
	namespace swap
	{
		namespace
		{
			const uint8_t MAGIC[4] = { 'S', '3', 'P', 'C' };
			const uint8_t VERSION = 1;

			// Encoding helpers, little-endian
			void put32(uint8_t* data, uint32_t value)	{ for (int k = 0; k < 4; ++k) data[k] = (uint8_t)(value >> (k * 8)); }
			void put64(uint8_t* data, uint64_t value)	{ for (int k = 0; k < 8; ++k) data[k] = (uint8_t)(value >> (k * 8)); }
			uint32_t get32(const uint8_t* data)			{ uint32_t value = 0; for (int k = 0; k < 4; ++k) value |= (uint32_t)data[k] << (k * 8); return value; }
			uint64_t get64(const uint8_t* data)			{ uint64_t value = 0; for (int k = 0; k < 8; ++k) value |= (uint64_t)data[k] << (k * 8); return value; }

			uint32_t checksum(const uint8_t* data, size_t size)
			{
				uint32_t hash = 2166136261u;		// FNV-1a
				for (size_t k = 0; k < size; ++k)
					hash = (hash ^ data[k]) * 16777619u;
				return hash;
			}
		}

		std::vector<uint8_t> encode(const Message& message)
		{
			// 0: magic, 4: version, 5: type, 6: funny index (or -1), 7: zone, 8: act, 9: picture 'a' / 'b', 12: sender ID, 20: cards,
			// 24: days, 28: name (UTF-8, up to 31 bytes, zero-terminated), 60: checksum of everything before
			std::vector<uint8_t> data(MESSAGE_SIZE, 0);
			memcpy(&data[0], MAGIC, 4);
			data[4] = VERSION;
			data[5] = (uint8_t)message.mType;
			data[6] = (uint8_t)(int8_t)message.mPostcard.mFunny;
			data[7] = (uint8_t)message.mPostcard.mZone;
			data[8] = (uint8_t)message.mPostcard.mAct;
			data[9] = (uint8_t)message.mPostcard.mImage;
			put64(&data[12], message.mSenderId);
			put32(&data[20], message.mCards);
			put32(&data[24], message.mDays);
			const size_t nameLength = std::min<size_t>(message.mName.size(), 31);
			memcpy(&data[28], message.mName.data(), nameLength);
			put32(&data[60], checksum(data.data(), 60));
			return data;
		}

		bool decode(const uint8_t* data, size_t size, Message& outMessage)
		{
			if (size < MESSAGE_SIZE || memcmp(data, MAGIC, 4) != 0 || data[4] != VERSION || get32(&data[60]) != checksum(data, 60))
				return false;
			if (data[5] != Message::CARD && data[5] != Message::THANKS)
				return false;
			outMessage = Message();
			outMessage.mType = (Message::Type)data[5];
			outMessage.mSenderId = get64(&data[12]);
			outMessage.mCards = get32(&data[20]);
			outMessage.mDays = get32(&data[24]);
			char name[32] = { 0 };
			memcpy(name, &data[28], 31);
			outMessage.mName = name;
			if (outMessage.mType == Message::THANKS)
				return true;

			// Only a postcard there is: checked by its name, like the journal file
			journal::Postcard postcard;
			postcard.mFunny = (int8_t)data[6];
			postcard.mZone = data[7];
			postcard.mAct = data[8];
			postcard.mImage = (char)data[9];
			if (postcard.mFunny >= journal::getNumFunnyPostcards() || postcard.mFunny < -1 || !journal::parsePostcardName(postcard.getSpriteName(), outMessage.mPostcard))
				return false;
			return true;
		}


#if !defined(N3DS_HOST_TEST)
		namespace
		{
			const uint32_t WLAN_COMM_ID = 0x53334143;			// This game's own ID for local wireless ("S3AC")
			const char PASSPHRASE[] = "Sonic 3 A.I.R. 3DS postcard swap";
			const uint8_t DATA_CHANNEL = 1;
			const int SEARCH_SECONDS = 60;
			const size_t APPDATA_SIZE = 48;			// In the network's beacon: magic, version, the host's swap ID and name

			struct Shared
			{
				LightLock mLock;
				Status mStatus;
			};
			Shared gShared;
			bool gSharedReady = false;
			volatile bool gStopRequested = false;
			volatile bool gThreadRunning = false;

			// Set by "start", for the thread
			Message gMine;
			std::vector<uint64_t> gSwappedToday;		// Partners already swapped with today

			struct Locked
			{
				Locked()  { LightLock_Lock(&gShared.mLock); }
				~Locked() { LightLock_Unlock(&gShared.mLock); }
			};

			uint64_t nowMs()
			{
				return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000);
			}

			void setStatus(Phase phase, const std::string& message = std::string(), int secondsLeft = 0)
			{
				Locked lock;
				gShared.mStatus.mPhase = phase;
				gShared.mStatus.mMessage = message;
				gShared.mStatus.mSecondsLeft = secondsLeft;
			}

			bool swappedToday(uint64_t id)
			{
				return std::find(gSwappedToday.begin(), gSwappedToday.end(), id) != gSwappedToday.end();
			}

			std::vector<uint8_t> makeAppData()
			{
				std::vector<uint8_t> appData(APPDATA_SIZE, 0);
				memcpy(&appData[0], MAGIC, 4);
				appData[4] = VERSION;
				put64(&appData[8], gMine.mSenderId);
				memcpy(&appData[16], gMine.mName.data(), std::min<size_t>(gMine.mName.size(), 31));
				return appData;
			}

			bool readAppData(const udsNetworkStruct& network, uint64_t& outId, std::string& outName)
			{
				uint8_t appData[APPDATA_SIZE] = { 0 };
				size_t size = 0;
				if (R_FAILED(udsGetNetworkStructApplicationData(&network, appData, sizeof(appData), &size)) || size < APPDATA_SIZE ||
					memcmp(appData, MAGIC, 4) != 0 || appData[4] != VERSION)
					return false;
				outId = get64(&appData[8]);
				char name[32] = { 0 };
				memcpy(name, &appData[16], 31);
				outName = name;
				return true;
			}

			// Connected: both send their card until they have the other's, and say thanks; true with the partner's card
			bool exchange(udsBindContext& bindContext, Message& outPartner)
			{
				setStatus(Phase::SWAPPING);
				const std::vector<uint8_t> card = encode(gMine);
				Message thanksMessage = gMine;
				thanksMessage.mType = Message::THANKS;
				const std::vector<uint8_t> thanks = encode(thanksMessage);

				bool gotCard = false;
				bool gotThanks = false;
				const uint64_t start = nowMs();
				uint64_t gotCardAt = 0;
				uint64_t nextSend = 0;
				std::vector<uint8_t> buffer(UDS_DATAFRAME_MAXSIZE);
				while (!gStopRequested)
				{
					const uint64_t now = nowMs();
					if (now >= nextSend)
					{
						// Again and again, as frames can get lost
						udsSendTo(UDS_BROADCAST_NETWORKNODEID, DATA_CHANNEL, UDS_SENDFLAG_Default, card.data(), card.size());
						if (gotCard)
							udsSendTo(UDS_BROADCAST_NETWORKNODEID, DATA_CHANNEL, UDS_SENDFLAG_Default, thanks.data(), thanks.size());
						nextSend = now + 250;
					}

					while (true)
					{
						size_t size = 0;
						u16 sender = 0;
						if (R_FAILED(udsPullPacket(&bindContext, buffer.data(), buffer.size(), &size, &sender)) || size == 0)
							break;
						Message message;
						if (!decode(buffer.data(), size, message) || message.mSenderId == gMine.mSenderId)
							continue;
						if (message.mType == Message::CARD && !gotCard)
						{
							outPartner = message;
							gotCard = true;
							gotCardAt = now;
							udsSendTo(UDS_BROADCAST_NETWORKNODEID, DATA_CHANNEL, UDS_SENDFLAG_Default, thanks.data(), thanks.size());
						}
						else if (message.mType == Message::THANKS)
						{
							gotThanks = true;
						}
					}

					if (gotCard && gotThanks)
						break;
					if (gotCard && now > gotCardAt + 2000)
						break;		// Their thanks got lost, or they left: the card is here anyway
					if (now > start + 6000)
						break;
					svcSleepThread(20 * 1000 * 1000);
				}

				// A few more thanks, so they know too
				for (int k = 0; gotCard && k < 4 && !gStopRequested; ++k)
				{
					udsSendTo(UDS_BROADCAST_NETWORKNODEID, DATA_CHANNEL, UDS_SENDFLAG_Default, thanks.data(), thanks.size());
					svcSleepThread(100 * 1000 * 1000);
				}
				return gotCard;
			}

			void finish(bool success, const std::string& message, const Message* partner = nullptr)
			{
				Locked lock;
				gShared.mStatus.mPhase = Phase::DONE;
				gShared.mStatus.mSuccess = success;
				gShared.mStatus.mMessage = message;
				if (nullptr != partner)
				{
					gShared.mStatus.mPartnerName = partner->mName;
					gShared.mStatus.mPartnerId = partner->mSenderId;
					gShared.mStatus.mReceived.mPostcard = partner->mPostcard;
					gShared.mStatus.mReceived.mFrom = partner->mName;
					gShared.mStatus.mReceived.mFromCards = partner->mCards;
					gShared.mStatus.mReceived.mFromDays = partner->mDays;
				}
			}

			void run()
			{
				Result result = udsInit(0x3000, nullptr);
				if (R_FAILED(result))
				{
					RMX_LOG_INFO("Postcard swap: udsInit failed with " << rmx::hexString(result, 8));
					finish(false, "Local wireless isn't available. Is wireless communication turned on?");
					return;
				}

				const uint64_t start = nowMs();
				const uint64_t deadline = start + SEARCH_SECONDS * 1000;
				const size_t scanBufferSize = 0x4000;
				void* scanBuffer = malloc(scanBufferSize);
				const std::vector<uint8_t> appData = makeAppData();
				uint32_t random = (uint32_t)(gMine.mSenderId ^ svcGetSystemTick());
				int failures = 0;
				std::string alreadySwappedName;
				bool connected = false;
				bool isHost = false;
				udsBindContext bindContext;

				while (!gStopRequested && nowMs() < deadline && !connected && failures < 6 && nullptr != scanBuffer)
				{
					setStatus(Phase::SEARCHING, std::string(), (int)((deadline - nowMs() + 999) / 1000));

					// Look for the other console's network...
					udsNetworkScanInfo* networks = nullptr;
					size_t totalNetworks = 0;
					memset(scanBuffer, 0, scanBufferSize);
					result = udsScanBeacons(scanBuffer, scanBufferSize, &networks, &totalNetworks, WLAN_COMM_ID, 0, nullptr, false);
					if (R_FAILED(result))
					{
						++failures;
						RMX_LOG_INFO("Postcard swap: scanning failed with " << rmx::hexString(result, 8));
					}
					for (size_t k = 0; k < totalNetworks && !connected && !gStopRequested; ++k)
					{
						uint64_t id = 0;
						std::string name;
						if (!readAppData(networks[k].network, id, name) || id == gMine.mSenderId)
							continue;
						if (swappedToday(id))
						{
							alreadySwappedName = name;
							continue;
						}
						result = udsConnectNetwork(&networks[k].network, PASSPHRASE, sizeof(PASSPHRASE), &bindContext, UDS_BROADCAST_NETWORKNODEID, UDSCONTYPE_Client, DATA_CHANNEL, UDS_DEFAULT_RECVBUFSIZE);
						if (R_SUCCEEDED(result))
						{
							connected = true;
							isHost = false;
							RMX_LOG_INFO("Postcard swap: joined " << name << "'s network");
						}
						else
						{
							RMX_LOG_INFO("Postcard swap: joining failed with " << rmx::hexString(result, 8));
						}
					}
					free(networks);
					if (connected || gStopRequested)
						break;

					// ...and if there's none, open one for a moment (a random time, so two consoles don't keep doing the same)
					udsNetworkStruct network;
					udsGenerateDefaultNetworkStruct(&network, WLAN_COMM_ID, 0, 2);
					result = udsCreateNetwork(&network, PASSPHRASE, sizeof(PASSPHRASE), &bindContext, DATA_CHANNEL, UDS_DEFAULT_RECVBUFSIZE);
					if (R_FAILED(result))
					{
						++failures;
						RMX_LOG_INFO("Postcard swap: creating a network failed with " << rmx::hexString(result, 8));
						svcSleepThread(300 * 1000 * 1000);
						continue;
					}
					udsSetApplicationData(appData.data(), appData.size());
					random = random * 1103515245u + 12345u;
					const uint64_t hostUntil = nowMs() + 1500 + ((random >> 8) % 2500);
					while (!gStopRequested && nowMs() < hostUntil)
					{
						udsConnectionStatus status;
						if (R_SUCCEEDED(udsGetConnectionStatus(&status)) && status.total_nodes >= 2)
						{
							connected = true;
							isHost = true;
							RMX_LOG_INFO("Postcard swap: another console joined");
							break;
						}
						svcSleepThread(50 * 1000 * 1000);
					}
					if (!connected)
					{
						udsDestroyNetwork();
						udsUnbind(&bindContext);
					}
				}
				free(scanBuffer);

				Message partner;
				const bool gotCard = connected && !gStopRequested && exchange(bindContext, partner);
				if (connected)
				{
					if (isHost)
						udsDestroyNetwork();
					else
						udsDisconnectNetwork();
					udsUnbind(&bindContext);
				}
				udsExit();

				if (gStopRequested)
					finish(false, "Cancelled.");
				else if (gotCard)
					finish(true, std::string(), &partner);
				else if (connected)
					finish(false, "The connection was lost. Try again, a bit closer together.");
				else if (failures >= 6 || nullptr == scanBuffer)
					finish(false, "Local wireless isn't working right now. Is wireless communication turned on?");
				else if (!alreadySwappedName.empty())
					finish(false, "You already swapped with " + alreadySwappedName + " today. Try again tomorrow!");
				else
					finish(false, "Nobody found. Both of you need to open Swap Postcards at the same time.");
			}

			void threadFunc(void*)
			{
				run();
				gThreadRunning = false;
			}
		}

		void start(const std::string& playerName)
		{
			stop();
			if (!gSharedReady)
			{
				LightLock_Init(&gShared.mLock);
				gSharedReady = true;
			}
			const journal::Postcard* shared = journal::getSharedPostcard();
			{
				Locked lock;
				gShared.mStatus = Status();
				gShared.mStatus.mPhase = Phase::SEARCHING;
				gShared.mStatus.mSecondsLeft = SEARCH_SECONDS;
			}
			if (nullptr == shared)
			{
				finish(false, "Complete a stamp card first: you need a postcard to share.");
				return;
			}

			const journal::Data& journal = journal::get();
			gMine = Message();
			gMine.mType = Message::CARD;
			gMine.mSenderId = journal::getSwapId();
			gMine.mName = playerName.empty() ? std::string("A player") : playerName;
			gMine.mPostcard = *shared;
			gMine.mCards = journal.mCardsCompleted;
			gMine.mDays = journal.mDaysPlayed;
			gSwappedToday.clear();
			for (const auto& partner : journal.mSwapPartners)
			{
				if (journal::hasSwappedToday(partner.first))
					gSwappedToday.push_back(partner.first);
			}

			gStopRequested = false;
			gThreadRunning = true;
			s32 mainPriority = 0x30;
			svcGetThreadPriority(&mainPriority, CUR_THREAD_HANDLE);
			const s32 priority = std::min<s32>(mainPriority + 1, 0x3f);
			Thread thread = threadCreate(threadFunc, nullptr, 0x8000, priority, 1, true);
			if (nullptr == thread)
				thread = threadCreate(threadFunc, nullptr, 0x8000, priority, -2, true);
			if (nullptr == thread)
			{
				gThreadRunning = false;
				finish(false, "Couldn't start local wireless.");
			}
		}

		void stop()
		{
			if (!gThreadRunning)
				return;
			gStopRequested = true;
			for (int k = 0; k < 1000 && gThreadRunning; ++k)		// Up to 5 seconds (a scan in progress has to end first)
				svcSleepThread(5 * 1000 * 1000);
		}

		Status getStatus()
		{
			if (!gSharedReady)
				return Status();
			Locked lock;
			return gShared.mStatus;
		}

		bool isRunning()
		{
			return gThreadRunning;
		}
#endif
	}
}

#endif
