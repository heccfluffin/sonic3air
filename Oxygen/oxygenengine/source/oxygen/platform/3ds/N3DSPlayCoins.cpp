/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: reading and spending the system's Play Coins.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSPlayCoins.h"

#include <3ds.h>
#include <string.h>


namespace n3ds
{
	namespace playcoins
	{
		namespace
		{
			// The Play Coins are stored by the system in "gamecoin.dat", in the shared extdata 0xF000000B on the NAND
			// (see https://www.3dbrew.org/wiki/Extdata). It is 0x14 bytes:
			//   0x00  u32  magic 0x4F00
			//   0x04  u16  total Play Coins
			//   0x06  u16  Play Coins obtained on the date below
			//   0x08  u32  step count when a coin was last obtained
			//   0x0C  u32  step count of that day
			//   0x10  u16  year, 0x12 u8 month, 0x13 u8 day
			//
			// The HOME Menu reads this file at every boot, and a corrupted file can stop the system from booting. So this code is
			// strict: it only ever changes the 2 bytes of the coin total, only after checking size and magic number, writes with
			// the standard flush flag, and reads the value back afterwards.
			const constexpr u32 FILE_SIZE = 0x14;
			const constexpr u32 MAGIC = 0x4F00;
			const constexpr u32 COINS_OFFSET = 0x04;
			const constexpr int MAX_COINS = 300;		// The system's limit

			struct OpenFile
			{
				FS_Archive mArchive = 0;
				Handle mFile = 0;
				bool mArchiveOpen = false;
				bool mFileOpen = false;

				~OpenFile()
				{
					if (mFileOpen)
						FSFILE_Close(mFile);
					if (mArchiveOpen)
						FSUSER_CloseArchive(mArchive);
				}

				bool open(bool forWriting)
				{
					// Shared extdata archive path: media type, extdata ID low, extdata ID high
					static const u32 archivePathData[3] = { MEDIATYPE_NAND, 0xF000000B, 0x00048000 };
					const FS_Path archivePath = { PATH_BINARY, sizeof(archivePathData), archivePathData };
					if (R_FAILED(FSUSER_OpenArchive(&mArchive, ARCHIVE_SHARED_EXTDATA, archivePath)))
						return false;
					mArchiveOpen = true;

					// Never create the file, only open the existing one
					const u32 openFlags = forWriting ? (FS_OPEN_READ | FS_OPEN_WRITE) : FS_OPEN_READ;
					if (R_FAILED(FSUSER_OpenFile(&mFile, mArchive, fsMakePath(PATH_ASCII, "/gamecoin.dat"), openFlags, 0)))
						return false;
					mFileOpen = true;
					return true;
				}

				// Reads the whole file and checks that it looks valid
				bool readAndCheck(u8* outData)
				{
					u64 size = 0;
					if (R_FAILED(FSFILE_GetSize(mFile, &size)) || size != FILE_SIZE)
						return false;
					u32 bytesRead = 0;
					if (R_FAILED(FSFILE_Read(mFile, &bytesRead, 0, outData, FILE_SIZE)) || bytesRead != FILE_SIZE)
						return false;
					u32 magic = 0;
					memcpy(&magic, outData, 4);
					return (magic == MAGIC);
				}
			};

			int getCoins(const u8* data)
			{
				return (int)(data[COINS_OFFSET] | (data[COINS_OFFSET + 1] << 8));
			}
		}

		bool read(int& outCoins, bool log)
		{
			OpenFile file;
			u8 data[FILE_SIZE];
			if (!file.open(false) || !file.readAndCheck(data))
			{
				if (log)
					RMX_LOG_INFO("Play Coins: could not read the Play Coin data");
				return false;
			}
			outCoins = std::min(getCoins(data), MAX_COINS);
			return true;
		}

		bool spend(int amount, int& outCoinsLeft)
		{
			OpenFile file;
			u8 data[FILE_SIZE];
			if (!file.open(true) || !file.readAndCheck(data))
			{
				RMX_LOG_INFO("Play Coins: could not open the Play Coin data for spending, nothing spent");
				return false;
			}

			const int coins = getCoins(data);
			outCoinsLeft = std::min(coins, MAX_COINS);
			if (amount <= 0 || coins > MAX_COINS || coins < amount)
				return false;

			// Write only the coin total
			const u16 newCoins = (u16)(coins - amount);
			const u8 newBytes[2] = { (u8)(newCoins & 0xff), (u8)(newCoins >> 8) };
			u32 bytesWritten = 0;
			if (R_FAILED(FSFILE_Write(file.mFile, &bytesWritten, COINS_OFFSET, newBytes, 2, FS_WRITE_FLUSH)) || bytesWritten != 2)
			{
				RMX_LOG_INFO("Play Coins: writing failed");
				return false;
			}

			// Read back to make sure
			u8 check[FILE_SIZE];
			if (!file.readAndCheck(check) || getCoins(check) != (int)newCoins || memcmp(check + 6, data + 6, FILE_SIZE - 6) != 0)
			{
				RMX_LOG_INFO("Play Coins: the value read back after spending is not as expected");
				return false;
			}

			outCoinsLeft = newCoins;
			RMX_LOG_INFO("Play Coins: spent " << amount << ", " << newCoins << " left");
			return true;
		}
	}
}

#endif
