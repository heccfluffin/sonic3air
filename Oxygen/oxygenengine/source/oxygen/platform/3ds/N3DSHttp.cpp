/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: HTTPS requests (see the header).
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSHttp.h"

#include <curl/curl.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#if !defined(N3DS_HOST_TEST)
	#include <3ds.h>
	#include "cacert_pem.h"		// Embedded by the build (net/cacert.pem)
#endif


namespace n3ds
{
	namespace http
	{
		namespace
		{
			// Says what's asking, without anything about the player
			const char* const USER_AGENT = "SonicAIR-3DS/1.0 (unofficial Nintendo 3DS port of Sonic 3 A.I.R.)";

			bool gGlobalInitDone = false;
			CURL* gHandle = nullptr;		// Reused, so the next request to the same server can skip the TLS handshake (slow on the 3DS)

			struct Transfer
			{
				std::string* mBody = nullptr;
				FILE* mFile = nullptr;
				size_t mMaxBytes = 0;
				Progress* mProgress = nullptr;
				bool mTooBig = false;
				bool mWriteFailed = false;
			};

			size_t writeCallback(char* data, size_t size, size_t count, void* user)
			{
				Transfer& transfer = *(Transfer*)user;
				const size_t bytes = size * count;
				if (nullptr != transfer.mFile)
				{
					if (fwrite(data, 1, bytes, transfer.mFile) != bytes)
					{
						transfer.mWriteFailed = true;
						return 0;
					}
				}
				else if (nullptr != transfer.mBody)
				{
					if (transfer.mBody->size() + bytes > transfer.mMaxBytes)
					{
						transfer.mTooBig = true;
						return 0;
					}
					transfer.mBody->append(data, bytes);
				}
				return bytes;
			}

			int progressCallback(void* user, curl_off_t totalDown, curl_off_t nowDown, curl_off_t, curl_off_t)
			{
				Transfer& transfer = *(Transfer*)user;
				if (nullptr != transfer.mProgress)
				{
					transfer.mProgress->mBytes = (uint64_t)nowDown;
					transfer.mProgress->mTotalBytes = (uint64_t)totalDown;
					if (transfer.mProgress->mCancel)
						return 1;		// Aborts the transfer
				}
				return 0;
			}

			CURL* getHandle()
			{
				if (!gGlobalInitDone)
				{
				#if !defined(N3DS_HOST_TEST)
					// devkitPro's mbedTLS gets its random numbers from the system's SSL service, which has to run for that
					const ::Result result = sslcInit(0);
					if (R_FAILED(result))
						RMX_LOG_INFO("HTTPS: sslcInit failed (" << rmx::hexString((uint32)result, 8) << "), secure connections won't work");
				#endif
					curl_global_init(CURL_GLOBAL_DEFAULT);
					gGlobalInitDone = true;
				}
				if (nullptr == gHandle)
					gHandle = curl_easy_init();
				else
					curl_easy_reset(gHandle);		// Keeps the open connections, forgets the options
				return gHandle;
			}

			Result perform(const std::string& url, Transfer& transfer)
			{
				Result result;
				CURL* curl = getHandle();
				if (nullptr == curl)
				{
					result.mError = "Couldn't start a connection (out of memory?).";
					return result;
				}

				char errorBuffer[CURL_ERROR_SIZE] = { 0 };
				curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
				curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
				curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);		// Downloads go through a redirect to the file server
				curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
				curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");		// gzip for the JSON answers, much less to receive
				curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
				curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);		// Give up if nothing arrives for 30 seconds
				curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
				curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
				curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 64L * 1024);
				curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
				curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeCallback);
				curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
				curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &progressCallback);
				curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
				curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
			#if defined(N3DS_HOST_TEST)
				if (const char* caFile = getenv("N3DS_TEST_CA_FILE"))
					curl_easy_setopt(curl, CURLOPT_CAINFO, caFile);
			#else
				curl_blob caBlob;
				caBlob.data = (void*)cacert_pem;
				caBlob.len = (size_t)cacert_pem_size;
				caBlob.flags = CURL_BLOB_NOCOPY;
				curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &caBlob);
			#endif

				const CURLcode code = curl_easy_perform(curl);
				curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.mStatusCode);

				if (code == CURLE_OK && result.mStatusCode >= 200 && result.mStatusCode < 300)
				{
					result.mSuccess = true;
					return result;
				}

				if (code == CURLE_OK)
				{
					result.mError = (result.mStatusCode == 404) ? "Not found on the server (maybe it was removed)." :
									(result.mStatusCode == 429) ? "The server says we asked too often - wait a minute and try again." :
									(result.mStatusCode >= 500) ? "The server has problems right now (error " + std::to_string(result.mStatusCode) + "). Try again later." :
									"The server answered with error " + std::to_string(result.mStatusCode) + ".";
				}
				else if (transfer.mTooBig)
				{
					result.mError = "The answer was unexpectedly big.";
				}
				else if (transfer.mWriteFailed)
				{
					result.mError = "Writing to the SD card failed - is it full?";
				}
				else if (code == CURLE_ABORTED_BY_CALLBACK)
				{
					result.mError = "Cancelled.";
				}
				else if (code == CURLE_COULDNT_RESOLVE_HOST || code == CURLE_COULDNT_CONNECT)
				{
					result.mError = "Couldn't reach the server. Is the 3DS connected to the Internet?";
				}
				else if (code == CURLE_OPERATION_TIMEDOUT)
				{
					result.mError = "The connection timed out. Try again (the Wi-Fi might be weak here).";
				}
				else if (code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_SSL_CACERT_BADFILE || code == CURLE_SSL_CONNECT_ERROR)
				{
					result.mError = "The secure connection failed. Is the 3DS's clock set right? (" + std::string(errorBuffer[0] ? errorBuffer : curl_easy_strerror(code)) + ")";
				}
				else
				{
					result.mError = std::string("Connection problem: ") + (errorBuffer[0] ? errorBuffer : curl_easy_strerror(code));
				}
				if (code != CURLE_OK && code != CURLE_ABORTED_BY_CALLBACK)
					closeConnections();		// Start over with a fresh connection next time
				return result;
			}
		}

		Result get(const std::string& url, std::string& outBody, Progress* progress, size_t maxBytes)
		{
			outBody.clear();
			Transfer transfer;
			transfer.mBody = &outBody;
			transfer.mMaxBytes = maxBytes;
			transfer.mProgress = progress;
			return perform(url, transfer);
		}

		Result download(const std::string& url, const std::string& filePath, Progress* progress)
		{
			Result result;
			FILE* file = fopen(filePath.c_str(), "wb");
			if (nullptr == file)
			{
				result.mError = "Couldn't write to the SD card.";
				return result;
			}
			setvbuf(file, nullptr, _IOFBF, 64 * 1024);
			Transfer transfer;
			transfer.mFile = file;
			transfer.mProgress = progress;
			result = perform(url, transfer);
			if (fclose(file) != 0 && result.mSuccess)
			{
				result.mSuccess = false;
				result.mError = "Writing to the SD card failed - is it full?";
			}
			if (!result.mSuccess)
				unlink(filePath.c_str());
			return result;
		}

		void closeConnections()
		{
			if (nullptr != gHandle)
			{
				curl_easy_cleanup(gHandle);
				gHandle = nullptr;
			}
		}

		std::string urlEncode(const std::string& text)
		{
			static const char* const HEX = "0123456789ABCDEF";
			std::string result;
			for (const char c : text)
			{
				const unsigned char u = (unsigned char)c;
				if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '-' || u == '_' || u == '.' || u == '~')
				{
					result += c;
				}
				else
				{
					result += '%';
					result += HEX[u >> 4];
					result += HEX[u & 15];
				}
			}
			return result;
		}
	}
}

#endif
