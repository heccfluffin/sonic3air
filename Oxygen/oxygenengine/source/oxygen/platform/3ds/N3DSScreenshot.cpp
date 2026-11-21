/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: screenshots of the top screen (L + R).
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSScreenshot.h"
#include "oxygen/platform/3ds/N3DSBottomScreen.h"
#include "oxygen/platform/3ds/N3DSFileWriter.h"
#include "oxygen/platform/3ds/N3DSJournal.h"
#include "oxygen/platform/3ds/N3DSNotificationLight.h"
#include "oxygen/platform/3ds/N3DSProfile.h"

#include <3ds.h>
#include <memory>
#include <stdio.h>
#include <sys/stat.h>
#include <zlib.h>


namespace n3ds
{
	namespace screenshot
	{
		namespace
		{
			bool gRequested = false;
			bool gSaving = false;		// One at a time

			void appendU32(std::vector<uint8_t>& out, uint32_t value)
			{
				out.push_back((uint8_t)(value >> 24));
				out.push_back((uint8_t)(value >> 16));
				out.push_back((uint8_t)(value >> 8));
				out.push_back((uint8_t)value);
			}

			void appendChunk(std::vector<uint8_t>& out, const char* type, const uint8_t* data, size_t size)
			{
				appendU32(out, (uint32_t)size);
				const size_t start = out.size();
				out.insert(out.end(), type, type + 4);
				if (size > 0)
					out.insert(out.end(), data, data + size);
				appendU32(out, (uint32_t)crc32(0, &out[start], (uInt)(out.size() - start)));
			}

			// A plain RGB PNG, with zlib (thread-safe, unlike sharing the engine's codecs)
			bool encodePNG(const std::vector<uint8_t>& rgb, int width, int height, std::vector<uint8_t>& outPNG)
			{
				// Each row: filter type 0 ("none"), then the pixels
				std::vector<uint8_t> raw;
				raw.reserve((size_t)(width * 3 + 1) * height);
				for (int y = 0; y < height; ++y)
				{
					raw.push_back(0);
					raw.insert(raw.end(), rgb.begin() + (size_t)y * width * 3, rgb.begin() + (size_t)(y + 1) * width * 3);
				}
				uLongf compressedSize = compressBound((uLong)raw.size());
				std::vector<uint8_t> compressed(compressedSize);
				if (compress2(compressed.data(), &compressedSize, raw.data(), (uLong)raw.size(), 6) != Z_OK)
					return false;

				static const uint8_t SIGNATURE[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
				outPNG.assign(SIGNATURE, SIGNATURE + 8);
				std::vector<uint8_t> header;
				appendU32(header, (uint32_t)width);
				appendU32(header, (uint32_t)height);
				header.insert(header.end(), { 8, 2, 0, 0, 0 });		// 8 bits per channel, RGB, deflate, standard filters, no interlace
				appendChunk(outPNG, "IHDR", header.data(), header.size());
				appendChunk(outPNG, "IDAT", compressed.data(), compressedSize);
				appendChunk(outPNG, "IEND", nullptr, 0);
				return true;
			}
		}

		void update()
		{
			// L and R together; the second of them just pressed, so holding both doesn't take more
			const u32 held = hidKeysHeld();
			const u32 down = hidKeysDown();
			if ((held & KEY_L) && (held & KEY_R) && (down & (KEY_L | KEY_R)) && !gSaving)
				gRequested = true;
		}

		void onPresent(const void* pixels, int width, int height, int pitchBytes)
		{
			if (!gRequested || nullptr == pixels || width <= 0 || height <= 0)
				return;
			gRequested = false;
			gSaving = true;

			// Copy the frame as RGB right away (the game draws the next one into the same memory)
			const std::shared_ptr<std::vector<uint8_t>> rgb = std::make_shared<std::vector<uint8_t>>((size_t)width * height * 3);
			uint8_t* dst = rgb->data();
			for (int y = 0; y < height; ++y)
			{
				const uint8_t* src = (const uint8_t*)pixels + (size_t)y * pitchBytes;
				for (int x = 0; x < width; ++x, src += 4, dst += 3)
				{
					dst[0] = src[0];
					dst[1] = src[1];
					dst[2] = src[2];
				}
			}

			// Like a camera flash
			n3ds::notificationlight::flash();

			// File name from the 3DS clock
			const n3ds::profile::LocalTime now = n3ds::profile::getLocalTime();
			char name[64];
			snprintf(name, sizeof(name), "s3air_%04d-%02d-%02d_%02d-%02d", now.mYear, now.mMonth, now.mDay, now.mHour, now.mMinute);

			// Encoding and writing in the background
			struct Result { bool mSuccess = false; std::string mPath; };
			const std::shared_ptr<Result> result = std::make_shared<Result>();
			const std::string baseName = name;
			n3ds::filewriter::runInBackground("saving a screenshot",
				[rgb, width, height, baseName, result]()
				{
					mkdir("sdmc:/3ds", 0777);
					mkdir("sdmc:/3ds/sonic3air", 0777);
					mkdir(FOLDER, 0777);

					// A free file name: seconds aren't in the clock, so count up
					std::string path;
					for (int number = 1; number < 1000; ++number)
					{
						path = std::string(FOLDER) + baseName + ((number == 1) ? std::string() : ("_" + std::to_string(number))) + ".png";
						struct stat info;
						if (stat(path.c_str(), &info) != 0)
							break;
					}

					std::vector<uint8_t> png;
					if (!encodePNG(*rgb, width, height, png))
						return;
					FILE* file = fopen(path.c_str(), "wb");
					if (nullptr == file)
						return;
					const bool written = (fwrite(png.data(), 1, png.size(), file) == png.size());
					result->mSuccess = (fclose(file) == 0) && written;
					result->mPath = path;
				},
				[result]()
				{
					gSaving = false;
					if (result->mSuccess)
					{
						RMX_LOG_INFO("Screenshot saved: " << result->mPath);
						n3ds::journal::addScreenshot();
						n3ds::bottomscreen::showResult(n3ds::bottomscreen::Result::SCREENSHOT_SAVED);
					}
					else
					{
						RMX_LOG_INFO("Screenshot could not be saved" << (result->mPath.empty() ? std::string() : (": " + result->mPath)));
						n3ds::bottomscreen::showResult(n3ds::bottomscreen::Result::FAILED);
					}
				});
		}
	}
}

#endif
