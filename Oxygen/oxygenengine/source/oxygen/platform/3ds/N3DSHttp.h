/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: HTTPS requests (libcurl with mbedTLS from devkitPro's portlibs), for the GameBanana mod browser.
*	Certificates are checked against Mozilla's list of root certificates, built into the game (build/_3ds/net/cacert.pem):
*	the 3DS's own list is too old for many sites today.
*	Calls block, so they belong in a thread of their own (see N3DSGameBanana). The sockets service has to be running (socInit).
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include <atomic>
#include <cstdint>
#include <string>


namespace n3ds
{
	namespace http
	{
		struct Progress
		{
			std::atomic<uint64_t> mBytes { 0 };
			std::atomic<uint64_t> mTotalBytes { 0 };	// 0 while unknown
			std::atomic<bool> mCancel { false };		// Set from another thread to stop the transfer
		};

		struct Result
		{
			bool mSuccess = false;
			long mStatusCode = 0;		// HTTP status, 0 if there was no answer
			std::string mError;			// For the player, if it failed
		};

		// Into memory (API answers, thumbnails); "maxBytes" protects against surprises
		Result get(const std::string& url, std::string& outBody, Progress* progress = nullptr, size_t maxBytes = 8 * 1024 * 1024);

		// Straight into a file (mod downloads); the file is removed again if it fails
		Result download(const std::string& url, const std::string& filePath, Progress* progress = nullptr);

		// Closes the connection kept open for the next request (from the thread that made the requests)
		void closeConnections();

		std::string urlEncode(const std::string& text);
	}
}

#endif
