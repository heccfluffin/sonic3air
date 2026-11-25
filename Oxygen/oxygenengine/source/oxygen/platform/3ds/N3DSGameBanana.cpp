/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: browsing and installing mods from GameBanana (see the header).
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSGameBanana.h"
#include "oxygen/platform/3ds/N3DSHttp.h"
#include "oxygen/platform/3ds/N3DSModServer.h"

#include <3ds.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <map>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include <turbojpeg.h>

#if defined(N3DS_HOST_TEST)
	#include <json/json.h>
#endif


namespace n3ds
{
	namespace gamebanana
	{
		namespace
		{
			const uint32_t GAME_ID = 6878;						// "Sonic 3 A.I.R." on GameBanana
			const int PER_PAGE = 15;							// What the search gives, so all listings page the same way
			const uint64_t MAX_DOWNLOAD_SIZE = 400ull * 1024 * 1024;

			std::string gBaseUrl = "https://gamebanana.com";
			std::string gModsDirectory;
			std::string gWorkDirectory;

			struct ListRequest
			{
				Listing mListing = Listing::POPULAR;
				std::string mSearch;
				int mPage = 1;
			};

			struct ThumbnailRequest
			{
				std::string mUrl;
				int mMaxWidth = 100;
			};

			struct Shared
			{
				LightLock mLock;
				LightEvent mWake;
				State mState;

				bool mHasListRequest = false;
				ListRequest mListRequest;
				uint32_t mDetailsRequest = 0;
				std::deque<ThumbnailRequest> mThumbnailRequests;
				std::deque<Thumbnail> mThumbnails;
				bool mHasInstallRequest = false;
				FileInfo mInstallFile;
				uint32_t mInstallModId = 0;
				int64_t mInstallModUpdated = 0;
				std::deque<std::string> mInstalled;
				std::map<uint32_t, InstalledMod> mInstalledMods;		// By GameBanana mod ID
			};
			Shared gShared;
			bool gSharedReady = false;
			http::Progress gDownloadProgress;

			volatile bool gStopRequested = false;
			volatile bool gThreadRunning = false;

			struct Locked
			{
				Locked()  { LightLock_Lock(&gShared.mLock); }
				~Locked() { LightLock_Unlock(&gShared.mLock); }
			};

			void initShared()
			{
				if (!gSharedReady)
				{
					LightLock_Init(&gShared.mLock);
					LightEvent_Init(&gShared.mWake, RESET_ONESHOT);
					gSharedReady = true;
				}
			}

			bool endsWithLower(const std::string& text, const char* suffix)
			{
				const size_t length = strlen(suffix);
				if (text.length() < length)
					return false;
				for (size_t i = 0; i < length; ++i)
				{
					if (tolower((unsigned char)text[text.length() - length + i]) != suffix[i])
						return false;
				}
				return true;
			}

			// --- JSON helpers ---

			bool parseJson(const std::string& text, Json::Value& outRoot, std::string& outError)
			{
				Json::CharReaderBuilder builder;
				std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
				std::string errors;
				if (!reader->parse(text.data(), text.data() + text.size(), &outRoot, &errors) || !outRoot.isObject())
				{
					outError = "GameBanana sent something unexpected (not the usual answer).";
					return false;
				}
				if (outRoot.isMember("_sErrorCode") || outRoot.isMember("_sErrorMessage"))
				{
					outError = "GameBanana says: " + toDisplayText(outRoot.get("_sErrorMessage", outRoot.get("_sErrorCode", "error")).asString());
					return false;
				}
				return true;
			}

			std::string getString(const Json::Value& value, const char* key)
			{
				const Json::Value& member = value[key];
				return member.isString() ? member.asString() : std::string();
			}

			int64_t getNumber(const Json::Value& value, const char* key)
			{
				const Json::Value& member = value[key];
				if (member.isIntegral())
					return member.asInt64();
				if (member.isDouble())
					return (int64_t)member.asDouble();
				if (member.isString())
					return strtoll(member.asCString(), nullptr, 10);
				return 0;
			}

			// A preview picture's address in the wanted size ("_sFile100", "_sFile220" or "_sFile530")
			std::string getImageUrl(const Json::Value& image, const char* sizeKey)
			{
				const std::string base = getString(image, "_sBaseUrl");
				std::string file = getString(image, sizeKey);
				if (file.empty())
					file = getString(image, "_sFile");
				return (base.empty() || file.empty()) ? std::string() : base + "/" + file;
			}

			// --- The thread ---

			void setListState(Load load, const std::string& error, const ListRequest& request, std::vector<ModSummary>* mods, int total)
			{
				Locked lock;
				if (gShared.mHasListRequest)
					return;		// A newer request is waiting already, this one is outdated
				State& state = gShared.mState;
				state.mListLoad = load;
				state.mListError = error;
				state.mListing = request.mListing;
				state.mSearch = request.mSearch;
				state.mPage = request.mPage;
				if (nullptr != mods)
				{
					state.mMods.swap(*mods);
					state.mTotalMods = total;
				}
			}

			void doList(const ListRequest& request)
			{
				std::string body;
				const http::Result result = http::get(getListUrl(request.mListing, request.mSearch, request.mPage), body);
				std::vector<ModSummary> mods;
				int total = 0;
				std::string error = result.mError;
				if (result.mSuccess && parseModList(body, mods, total, error))
					setListState(Load::READY, "", request, &mods, total);
				else
					setListState(Load::FAILED, error, request, nullptr, 0);
			}

			void doDetails(uint32_t modId)
			{
				std::string body;
				const http::Result result = http::get(getDetailsUrl(modId), body);
				ModDetails details;
				std::string error = result.mError;
				const bool success = result.mSuccess && parseModDetails(body, details, error);
				Locked lock;
				if (gShared.mDetailsRequest != 0)
					return;		// Outdated
				if (success)
				{
					details.mId = modId;
					gShared.mState.mDetails = details;
					gShared.mState.mDetailsLoad = Load::READY;
				}
				else
				{
					gShared.mState.mDetailsError = error;
					gShared.mState.mDetailsLoad = Load::FAILED;
				}
			}

			void doThumbnail(const ThumbnailRequest& request)
			{
				Thumbnail thumbnail;
				thumbnail.mUrl = request.mUrl;
				std::string body;
				const http::Result result = http::get(request.mUrl, body, nullptr, 2 * 1024 * 1024);
				if (!result.mSuccess || !decodeJpeg(body, request.mMaxWidth, thumbnail))
					thumbnail.mFailed = true;
				thumbnail.mUrl = request.mUrl;
				Locked lock;
				gShared.mThumbnails.push_back(std::move(thumbnail));
			}

			void setInstallProgress(int done, int total)
			{
				Locked lock;
				gShared.mState.mFilesDone = done;
				gShared.mState.mFilesTotal = total;
			}

			const char* const INSTALLED_NOTE = "3ds_gamebanana.txt";

			// Which mods in the mods folder came from GameBanana (a note in each of their folders)
			void scanInstalledMods()
			{
				std::map<uint32_t, InstalledMod> found;
				if (DIR* dir = opendir(gModsDirectory.substr(0, gModsDirectory.size() - 1).c_str()))
				{
					std::vector<std::string> folders;
					while (const dirent* entry = readdir(dir))
					{
						if (entry->d_name[0] != '.')
							folders.emplace_back(entry->d_name);
					}
					closedir(dir);
					for (const std::string& folder : folders)
					{
						InstalledMod mod;
						if (readInstalledNote(gModsDirectory + folder, mod))
						{
							mod.mFolder = folder;
							found[mod.mModId] = mod;
						}
					}
				}
				Locked lock;
				gShared.mInstalledMods.swap(found);
			}

			void finishInstall(bool success, const std::string& message, const std::vector<std::string>& warnings, const std::string& folder)
			{
				RMX_LOG_INFO("GameBanana: " << message);
				Locked lock;
				State& state = gShared.mState;
				state.mInstall = Install::DONE;
				state.mInstallSuccess = success;
				state.mInstallMessage = message;
				state.mInstallWarnings = warnings;
				if (success)
					gShared.mInstalled.push_back(folder);
			}

			void doInstall(const FileInfo& file, const std::string& modName, uint32_t modId, int64_t modUpdated)
			{
				if (const char* problem = getFileProblem(file))
				{
					finishInstall(false, problem, {}, "");
					return;
				}

				RMX_LOG_INFO("GameBanana: downloading " << file.mName << " (" << file.mSize << " bytes) for \"" << modName << "\"");
				const std::string zipPath = gWorkDirectory + "gamebanana_download.bin";		// .zip, .rar or .7z: told by its first bytes
				mkdir(gWorkDirectory.substr(0, gWorkDirectory.size() - 1).c_str(), 0777);
				gDownloadProgress.mBytes = 0;
				gDownloadProgress.mTotalBytes = file.mSize;
				gDownloadProgress.mCancel = false;
				const http::Result result = http::download(file.mUrl, zipPath, &gDownloadProgress);
				if (!result.mSuccess)
				{
					finishInstall(false, (result.mError == "Cancelled.") ? "Download cancelled." : "The download failed: " + result.mError, {}, "");
					return;
				}

				{
					Locked lock;
					gShared.mState.mInstall = Install::UNPACKING;
					gShared.mState.mFilesDone = gShared.mState.mFilesTotal = 0;
				}
				const modserver::InstallResult installed = modserver::installZip(zipPath, file.mName, gModsDirectory, gWorkDirectory + "gamebanana_install/", &setInstallProgress);
				unlink(zipPath.c_str());
				if (installed.mSuccess && modId != 0)
				{
					// Noted in its folder, for "installed" and "update available" in the browser
					InstalledMod note;
					note.mModId = modId;
					note.mFileId = file.mId;
					note.mModUpdated = modUpdated;
					note.mFolder = installed.mFolder;
					writeInstalledNote(gModsDirectory + installed.mFolder, note);
					Locked lock;
					for (auto it = gShared.mInstalledMods.begin(); it != gShared.mInstalledMods.end(); )
						it = (it->second.mFolder == note.mFolder) ? gShared.mInstalledMods.erase(it) : std::next(it);		// Replaced
					gShared.mInstalledMods[modId] = note;
				}
				finishInstall(installed.mSuccess, installed.mMessage, installed.mWarnings, installed.mFolder);
			}

			void threadFunc(void*)
			{
				scanInstalledMods();
				while (!gStopRequested)
				{
					// Most important first: installing, then details, then the list, then pictures
					bool hasInstall = false;
					FileInfo installFile;
					std::string installName;
					uint32_t installModId = 0;
					int64_t installModUpdated = 0;
					uint32_t detailsId = 0;
					bool hasList = false;
					ListRequest listRequest;
					bool hasThumbnail = false;
					ThumbnailRequest thumbnailRequest;
					{
						Locked lock;
						if (gShared.mHasInstallRequest)
						{
							hasInstall = true;
							installFile = gShared.mInstallFile;
							installName = gShared.mState.mInstallName;
							installModId = gShared.mInstallModId;
							installModUpdated = gShared.mInstallModUpdated;
							gShared.mHasInstallRequest = false;
						}
						else if (gShared.mDetailsRequest != 0)
						{
							detailsId = gShared.mDetailsRequest;
							gShared.mDetailsRequest = 0;
						}
						else if (gShared.mHasListRequest)
						{
							hasList = true;
							listRequest = gShared.mListRequest;
							gShared.mHasListRequest = false;
						}
						else if (!gShared.mThumbnailRequests.empty())
						{
							hasThumbnail = true;
							thumbnailRequest = gShared.mThumbnailRequests.front();
							gShared.mThumbnailRequests.pop_front();
						}
					}

					if (hasInstall)
						doInstall(installFile, installName, installModId, installModUpdated);
					else if (detailsId != 0)
						doDetails(detailsId);
					else if (hasList)
						doList(listRequest);
					else if (hasThumbnail)
						doThumbnail(thumbnailRequest);
					else
						LightEvent_WaitTimeout(&gShared.mWake, 250 * 1000 * 1000LL);
				}
				http::closeConnections();
				gThreadRunning = false;
			}

			void wake()
			{
				LightEvent_Signal(&gShared.mWake);
			}

			// One character (code point) as something the game's fonts can show
			void appendDisplayChar(std::string& out, uint32_t c)
			{
				if (c == '\t')
				{
					out += ' ';
				}
				else if (c == '\n' || (c >= 0x20 && c < 0x7f))
				{
					out += (char)c;
				}
				else if (c >= 0xc0 && c <= 0xff)
				{
					// Latin-1 letters without their accents
					static const char* const LATIN = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
					out += LATIN[c - 0xc0];
				}
				else
				{
					switch (c)
					{
						case 0xa0:  out += ' ';  break;
						case 0xa9:  out += "(c)";  break;
						case 0xae:  out += "(R)";  break;
						case 0xb0:  out += " deg";  break;
						case 0x2018: case 0x2019: case 0x201a: case 0x2032:  out += '\'';  break;
						case 0x201c: case 0x201d: case 0x201e: case 0x2033:  out += '"';  break;
						case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212:  out += '-';  break;
						case 0x2022: case 0x25cf: case 0x25aa:  out += '*';  break;
						case 0x2026:  out += "...";  break;
						case 0x2122:  out += "(TM)";  break;
						case 0x2190:  out += "<-";  break;
						case 0x2192:  out += "->";  break;
						default:  break;		// Emoji, other scripts: left out
					}
				}
			}
		}

		void start(const std::string& modsDirectory, const std::string& workDirectory)
		{
			initShared();
			if (gThreadRunning)
				return;
			gModsDirectory = modsDirectory;
			gWorkDirectory = workDirectory;
			{
				Locked lock;
				gShared.mState = State();
				gShared.mHasListRequest = false;
				gShared.mDetailsRequest = 0;
				gShared.mThumbnailRequests.clear();
				gShared.mThumbnails.clear();
				gShared.mHasInstallRequest = false;
			}

			gStopRequested = false;
			gThreadRunning = true;
			s32 mainPriority = 0x30;
			svcGetThreadPriority(&mainPriority, CUR_THREAD_HANDLE);
			const s32 priority = std::min<s32>(mainPriority + 1, 0x3f);
			// The New 3DS's extra core if possible (TLS handshakes and JPEG decoding need some computing), else the system core
			Thread thread = threadCreate(threadFunc, nullptr, 0x10000, priority, 2, true);
			if (nullptr == thread)
				thread = threadCreate(threadFunc, nullptr, 0x10000, priority, 1, true);
			if (nullptr == thread)
				thread = threadCreate(threadFunc, nullptr, 0x10000, priority, -2, true);
			if (nullptr == thread)
			{
				gThreadRunning = false;
				RMX_LOG_INFO("GameBanana: couldn't start the thread");
			}
		}

		void stop()
		{
			if (!gThreadRunning)
				return;
			gStopRequested = true;
			gDownloadProgress.mCancel = true;
			wake();
			for (int k = 0; k < 1000 && gThreadRunning; ++k)		// Up to 5 seconds for a request to finish
				svcSleepThread(5 * 1000 * 1000);
		}

		State getState()
		{
			if (!gSharedReady)
				return State();
			Locked lock;
			State state = gShared.mState;
			if (state.mInstall == Install::DOWNLOADING)
			{
				state.mBytes = gDownloadProgress.mBytes;
				const uint64_t total = gDownloadProgress.mTotalBytes;
				if (total > 0)
					state.mTotalBytes = total;
			}
			return state;
		}

		void requestList(Listing listing, const std::string& search, int page)
		{
			initShared();
			{
				Locked lock;
				gShared.mListRequest.mListing = listing;
				gShared.mListRequest.mSearch = search;
				gShared.mListRequest.mPage = std::max(page, 1);
				gShared.mHasListRequest = true;
				State& state = gShared.mState;
				state.mListLoad = Load::LOADING;
				state.mListing = listing;
				state.mSearch = search;
				state.mPage = std::max(page, 1);
				state.mPerPage = PER_PAGE;
				gShared.mThumbnailRequests.clear();		// The old page's pictures aren't needed any more
			}
			wake();
		}

		void requestDetails(uint32_t modId)
		{
			initShared();
			{
				Locked lock;
				gShared.mDetailsRequest = modId;
				gShared.mState.mDetailsLoad = Load::LOADING;
				gShared.mState.mDetails = ModDetails();
				gShared.mState.mDetails.mId = modId;
			}
			wake();
		}

		void requestThumbnail(const std::string& url, int maxWidth)
		{
			if (url.empty())
				return;
			initShared();
			{
				Locked lock;
				for (const ThumbnailRequest& request : gShared.mThumbnailRequests)
				{
					if (request.mUrl == url)
						return;
				}
				ThumbnailRequest request;
				request.mUrl = url;
				request.mMaxWidth = maxWidth;
				gShared.mThumbnailRequests.push_back(request);
			}
			wake();
		}

		void clearThumbnailRequests()
		{
			if (!gSharedReady)
				return;
			Locked lock;
			gShared.mThumbnailRequests.clear();
		}

		bool takeThumbnail(Thumbnail& outThumbnail)
		{
			if (!gSharedReady)
				return false;
			Locked lock;
			if (gShared.mThumbnails.empty())
				return false;
			outThumbnail = std::move(gShared.mThumbnails.front());
			gShared.mThumbnails.pop_front();
			return true;
		}

		void requestInstall(const FileInfo& file, const std::string& modName, uint32_t modId, int64_t modUpdated)
		{
			initShared();
			{
				Locked lock;
				if (gShared.mState.mInstall == Install::DOWNLOADING || gShared.mState.mInstall == Install::UNPACKING)
					return;
				gShared.mInstallFile = file;
				gShared.mInstallModId = modId;
				gShared.mInstallModUpdated = modUpdated;
				gShared.mHasInstallRequest = true;
				State& state = gShared.mState;
				state.mInstall = Install::DOWNLOADING;
				state.mInstallName = modName;
				state.mBytes = 0;
				state.mTotalBytes = file.mSize;
				state.mFilesDone = state.mFilesTotal = 0;
				state.mInstallSuccess = false;
				state.mInstallMessage.clear();
				state.mInstallWarnings.clear();
				gDownloadProgress.mBytes = 0;
				gDownloadProgress.mTotalBytes = file.mSize;
				gDownloadProgress.mCancel = false;
			}
			wake();
		}

		void cancelInstall()
		{
			gDownloadProgress.mCancel = true;		// Only the download can be stopped; unpacking finishes, or the mod would be half there
		}

		void acknowledgeInstall()
		{
			if (!gSharedReady)
				return;
			Locked lock;
			if (gShared.mState.mInstall == Install::DONE)
				gShared.mState.mInstall = Install::IDLE;
		}

		bool takeInstalledMod(std::string& outFolder)
		{
			if (!gSharedReady)
				return false;
			Locked lock;
			if (gShared.mInstalled.empty())
				return false;
			outFolder = gShared.mInstalled.front();
			gShared.mInstalled.pop_front();
			return true;
		}

		bool getInstalledMod(uint32_t modId, InstalledMod& outMod)
		{
			if (!gSharedReady || modId == 0)
				return false;
			Locked lock;
			const auto it = gShared.mInstalledMods.find(modId);
			if (it == gShared.mInstalledMods.end())
				return false;
			outMod = it->second;
			return true;
		}

		bool readInstalledNote(const std::string& modFolderPath, InstalledMod& outMod)
		{
			FILE* file = fopen((modFolderPath + "/" + INSTALLED_NOTE).c_str(), "rb");
			if (nullptr == file)
				return false;
			char line[128];
			InstalledMod mod;
			while (fgets(line, sizeof(line), file))
			{
				unsigned int value = 0;
				long long date = 0;
				if (sscanf(line, "mod=%u", &value) == 1)			mod.mModId = value;
				else if (sscanf(line, "file=%u", &value) == 1)		mod.mFileId = value;
				else if (sscanf(line, "updated=%lld", &date) == 1)	mod.mModUpdated = date;
			}
			fclose(file);
			if (mod.mModId == 0)
				return false;
			outMod = mod;
			return true;
		}

		bool writeInstalledNote(const std::string& modFolderPath, const InstalledMod& mod)
		{
			FILE* file = fopen((modFolderPath + "/" + INSTALLED_NOTE).c_str(), "wb");
			if (nullptr == file)
				return false;
			fprintf(file, "# Installed from GameBanana by the 3DS port's mod browser (https://gamebanana.com/mods/%u)\nmod=%u\nfile=%u\nupdated=%lld\n",
					(unsigned)mod.mModId, (unsigned)mod.mModId, (unsigned)mod.mFileId, (long long)mod.mModUpdated);
			return (fclose(file) == 0);
		}

		void setBaseUrl(const std::string& baseUrl)
		{
			gBaseUrl = baseUrl;
		}

		std::string getListUrl(Listing listing, const std::string& search, int page)
		{
			const std::string pageText = std::to_string(std::max(page, 1));
			if (listing == Listing::SEARCH)
			{
				return gBaseUrl + "/apiv11/Util/Search/Results?_sModelName=Mod&_sOrder=best_match&_idGameRow=" + std::to_string(GAME_ID) +
					   "&_sSearchString=" + http::urlEncode(search) + "&_nPage=" + pageText;
			}
			const char* sort = (listing == Listing::NEWEST) ? "Generic_Newest" : (listing == Listing::TOP_RATED) ? "Generic_MostLiked" : "Generic_MostDownloaded";
			return gBaseUrl + "/apiv11/Mod/Index?_nPage=" + pageText + "&_nPerpage=" + std::to_string(PER_PAGE) +
				   "&_aFilters%5BGeneric_Game%5D=" + std::to_string(GAME_ID) + "&_sSort=" + sort;
		}

		std::string getDetailsUrl(uint32_t modId)
		{
			return gBaseUrl + "/apiv11/Mod/" + std::to_string(modId) +
				   "?_csvProperties=_sName,_aSubmitter,_sText,_sDescription,_aFiles,_nDownloadCount,_nLikeCount,_nViewCount,_sVersion,"
				   "_tsDateUpdated,_tsDateAdded,_aPreviewMedia,_aRootCategory,_bIsObsolete";
		}

		bool parseModList(const std::string& json, std::vector<ModSummary>& outMods, int& outTotal, std::string& outError)
		{
			outMods.clear();
			outTotal = 0;
			Json::Value root;
			if (!parseJson(json, root, outError))
				return false;
			const Json::Value& records = root["_aRecords"];
			if (!records.isArray())
			{
				outError = "GameBanana sent a list without mods in it.";
				return false;
			}
			for (const Json::Value& record : records)
			{
				if (!record.isObject() || getString(record, "_sModelName") != "Mod")
					continue;
				ModSummary mod;
				mod.mId = (uint32_t)getNumber(record, "_idRow");
				mod.mName = toDisplayText(getString(record, "_sName"));
				mod.mAuthor = toDisplayText(getString(record["_aSubmitter"], "_sName"));
				mod.mCategory = toDisplayText(getString(record["_aRootCategory"], "_sName"));
				mod.mLikes = (int)getNumber(record, "_nLikeCount");
				mod.mViews = (int)getNumber(record, "_nViewCount");
				mod.mDateAdded = getNumber(record, "_tsDateAdded");
				mod.mDateUpdated = std::max(getNumber(record, "_tsDateUpdated"), mod.mDateAdded);
				mod.mHasFiles = !record.isMember("_bHasFiles") || record["_bHasFiles"].asBool();
				mod.mObsolete = record["_bIsObsolete"].isBool() && record["_bIsObsolete"].asBool();
				const Json::Value& images = record["_aPreviewMedia"]["_aImages"];
				if (images.isArray() && !images.empty())
					mod.mThumbnailUrl = getImageUrl(images[0], "_sFile100");
				if (mod.mId != 0)
					outMods.push_back(mod);
			}
			outTotal = (int)getNumber(root["_aMetadata"], "_nRecordCount");
			return true;
		}

		bool parseModDetails(const std::string& json, ModDetails& outDetails, std::string& outError)
		{
			outDetails = ModDetails();
			Json::Value root;
			if (!parseJson(json, root, outError))
				return false;
			outDetails.mName = toDisplayText(getString(root, "_sName"));
			outDetails.mAuthor = toDisplayText(getString(root["_aSubmitter"], "_sName"));
			outDetails.mCategory = toDisplayText(getString(root["_aRootCategory"], "_sName"));
			outDetails.mVersion = toDisplayText(getString(root, "_sVersion"));
			outDetails.mSummary = toDisplayText(getString(root, "_sDescription"));
			outDetails.mText = toDisplayText(htmlToText(getString(root, "_sText")));
			outDetails.mLikes = (int)getNumber(root, "_nLikeCount");
			outDetails.mViews = (int)getNumber(root, "_nViewCount");
			outDetails.mDownloads = (int)getNumber(root, "_nDownloadCount");
			outDetails.mDateAdded = getNumber(root, "_tsDateAdded");
			outDetails.mDateUpdated = std::max(getNumber(root, "_tsDateUpdated"), outDetails.mDateAdded);
			outDetails.mObsolete = root["_bIsObsolete"].isBool() && root["_bIsObsolete"].asBool();

			const Json::Value& files = root["_aFiles"];
			if (files.isArray())
			{
				for (const Json::Value& entry : files)
				{
					FileInfo file;
					file.mId = (uint32_t)getNumber(entry, "_idRow");
					file.mName = getString(entry, "_sFile");
					file.mUrl = getString(entry, "_sDownloadUrl");
					file.mDescription = toDisplayText(getString(entry, "_sDescription"));
					file.mSize = (uint64_t)getNumber(entry, "_nFilesize");
					file.mDownloads = (int)getNumber(entry, "_nDownloadCount");
					file.mDateAdded = getNumber(entry, "_tsDateAdded");
					const std::string av = getString(entry, "_sAvResult");
					const std::string analysis = getString(entry, "_sAnalysisResult");
					file.mFlagged = (!av.empty() && av != "clean") || (!analysis.empty() && analysis != "ok");
					if (!file.mUrl.empty())
						outDetails.mFiles.push_back(file);
				}
				// Newest first, that's usually the one to get
				std::stable_sort(outDetails.mFiles.begin(), outDetails.mFiles.end(), [](const FileInfo& a, const FileInfo& b) { return a.mDateAdded > b.mDateAdded; });
			}

			const Json::Value& images = root["_aPreviewMedia"]["_aImages"];
			if (images.isArray())
			{
				for (const Json::Value& image : images)
				{
					const std::string url = getImageUrl(image, "_sFile220");
					if (!url.empty() && outDetails.mImageUrls.size() < 8)
						outDetails.mImageUrls.push_back(url);
				}
			}
			return true;
		}

		std::string htmlToText(const std::string& html)
		{
			std::string text;
			text.reserve(html.size());
			for (size_t i = 0; i < html.size(); )
			{
				const char c = html[i];
				if (c == '<')
				{
					const size_t end = html.find('>', i);
					if (end == std::string::npos)
						break;
					std::string tag = html.substr(i + 1, end - i - 1);
					for (char& t : tag)
						t = (char)tolower((unsigned char)t);
					const bool closing = !tag.empty() && tag[0] == '/';
					std::string name = tag.substr(closing ? 1 : 0);
					name = name.substr(0, name.find_first_of(" /\t\n"));
					if (name == "br" || name == "p" || name == "div" || name == "h1" || name == "h2" || name == "h3" || name == "h4" || name == "ul" || name == "ol" || name == "tr" || name == "blockquote")
						text += '\n';
					else if (name == "li" && !closing)
						text += "\n- ";
					else if (name == "img" && !closing)
						text += "[picture]";
					i = end + 1;
				}
				else if (c == '&')
				{
					const size_t end = html.find(';', i);
					if (end != std::string::npos && end - i <= 8)
					{
						const std::string entity = html.substr(i + 1, end - i - 1);
						uint32_t code = 0;
						if (entity == "amp")		code = '&';
						else if (entity == "lt")	code = '<';
						else if (entity == "gt")	code = '>';
						else if (entity == "quot")	code = '"';
						else if (entity == "apos")	code = '\'';
						else if (entity == "nbsp")	code = ' ';
						else if (entity == "hellip")	code = 0x2026;
						else if (entity == "mdash" || entity == "ndash")	code = '-';
						else if (entity == "rsquo" || entity == "lsquo")	code = '\'';
						else if (entity == "rdquo" || entity == "ldquo")	code = '"';
						else if (!entity.empty() && entity[0] == '#')
							code = (uint32_t)((entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')) ? strtoul(entity.c_str() + 2, nullptr, 16) : strtoul(entity.c_str() + 1, nullptr, 10));
						if (code != 0)
						{
							// Back to UTF-8, "toDisplayText" handles the rest
							if (code < 0x80)
								text += (char)code;
							else if (code < 0x800)
							{
								text += (char)(0xc0 | (code >> 6));
								text += (char)(0x80 | (code & 0x3f));
							}
							else if (code < 0x10000)
							{
								text += (char)(0xe0 | (code >> 12));
								text += (char)(0x80 | ((code >> 6) & 0x3f));
								text += (char)(0x80 | (code & 0x3f));
							}
							i = end + 1;
							continue;
						}
					}
					text += c;
					++i;
				}
				else
				{
					text += (c == '\r' || c == '\n' || c == '\t') ? ' ' : c;
					++i;
				}
			}

			// Tidy up: spaces at line starts and ends, at most one empty line in a row
			std::string result;
			std::string line;
			int emptyLines = 1;		// No empty lines at the start
			const auto flushLine = [&]()
			{
				size_t first = line.find_first_not_of(' ');
				if (first == std::string::npos)
				{
					if (emptyLines++ == 0)
						result += '\n';
				}
				else
				{
					size_t last = line.find_last_not_of(' ');
					std::string cleaned;
					for (size_t k = first; k <= last; ++k)
					{
						if (line[k] == ' ' && !cleaned.empty() && cleaned.back() == ' ')
							continue;
						cleaned += line[k];
					}
					result += cleaned + '\n';
					emptyLines = 0;
				}
				line.clear();
			};
			for (const char c : text)
			{
				if (c == '\n')
					flushLine();
				else
					line += c;
			}
			flushLine();
			while (!result.empty() && result.back() == '\n')
				result.pop_back();
			return result;
		}

		std::string toDisplayText(const std::string& utf8)
		{
			std::string out;
			out.reserve(utf8.size());
			for (size_t i = 0; i < utf8.size(); )
			{
				const unsigned char c = (unsigned char)utf8[i];
				uint32_t code = c;
				int extra = 0;
				if (c >= 0xf0)		{ code = c & 0x07; extra = 3; }
				else if (c >= 0xe0)	{ code = c & 0x0f; extra = 2; }
				else if (c >= 0xc0)	{ code = c & 0x1f; extra = 1; }
				else if (c >= 0x80)	{ ++i; continue; }		// Stray byte
				if (i + extra >= utf8.size() && extra > 0)
					break;		// Cut off in the middle of a character
				for (int k = 1; k <= extra; ++k)
					code = (code << 6) | ((unsigned char)utf8[i + k] & 0x3f);
				i += 1 + extra;
				if (code == '\r')
					continue;
				appendDisplayChar(out, code);
			}
			return out;
		}

		const char* getFileProblem(const FileInfo& file)
		{
			const std::string& name = file.mName;
			if (!endsWithLower(name, ".zip") && !endsWithLower(name, ".rar") && !endsWithLower(name, ".7z"))
				return "This file isn't a .zip, .rar or .7z, so it's not a mod the 3DS can install.";
			if (file.mSize > MAX_DOWNLOAD_SIZE)
				return "This file is too big for the 3DS (over 400 MB).";
			return nullptr;
		}

		bool decodeJpeg(const std::string& data, int maxWidth, Thumbnail& outThumbnail)
		{
			if (data.size() < 4 || (unsigned char)data[0] != 0xff || (unsigned char)data[1] != 0xd8)
				return false;		// Not a JPEG (GameBanana has a few WebP pictures)
			tjhandle handle = tjInitDecompress();
			if (nullptr == handle)
				return false;
			int width = 0, height = 0, subsampling = 0, colorspace = 0;
			bool success = false;
			if (tjDecompressHeader3(handle, (const unsigned char*)data.data(), (unsigned long)data.size(), &width, &height, &subsampling, &colorspace) == 0 && width > 0 && height > 0)
			{
				// libjpeg can scale down while decoding (1/2, 1/4, 1/8), much faster than decoding all
				int scaledWidth = width, scaledHeight = height;
				int numFactors = 0;
				const tjscalingfactor* factors = tjGetScalingFactors(&numFactors);
				for (int k = 0; k < numFactors; ++k)
				{
					const int w = TJSCALED(width, factors[k]);
					const int h = TJSCALED(height, factors[k]);
					if (factors[k].num <= factors[k].denom && w >= maxWidth && w < scaledWidth)
					{
						scaledWidth = w;
						scaledHeight = h;
					}
				}
				if (scaledWidth <= 2048 && scaledHeight <= 2048)
				{
					outThumbnail.mPixels.resize((size_t)scaledWidth * scaledHeight);
					if (tjDecompress2(handle, (const unsigned char*)data.data(), (unsigned long)data.size(), (unsigned char*)outThumbnail.mPixels.data(), scaledWidth, 0, scaledHeight, TJPF_RGBA, TJFLAG_FASTDCT) == 0)
					{
						outThumbnail.mWidth = scaledWidth;
						outThumbnail.mHeight = scaledHeight;
						success = true;
					}
				}
			}
			tjDestroy(handle);
			if (!success)
				outThumbnail.mPixels.clear();
			return success;
		}
	}
}

#endif
