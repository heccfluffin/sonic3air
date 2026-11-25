/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: browsing and installing Sonic 3 A.I.R. mods from GameBanana (gamebanana.com), right in the Mods menu.
*	Uses GameBanana's public API (no account needed): lists sorted by downloads, date or likes, search, a mod's details with its
*	files, preview pictures. A mod's .zip is downloaded to the SD card and unpacked by the same code as the Wi-Fi install
*	(see N3DSModServer). Everything that waits for the network runs in a thread of its own; the menu asks for things and checks
*	the state each frame.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include <cstdint>
#include <string>
#include <vector>


namespace n3ds
{
	namespace gamebanana
	{
		enum class Listing
		{
			POPULAR,		// Most downloaded
			NEWEST,
			TOP_RATED,		// Most liked
			SEARCH
		};

		struct ModSummary
		{
			uint32_t mId = 0;
			std::string mName;
			std::string mAuthor;
			std::string mCategory;
			std::string mThumbnailUrl;		// Small preview picture (100 pixels wide), or empty
			int mLikes = 0;
			int mViews = 0;
			int64_t mDateAdded = 0;			// Unix time
			int64_t mDateUpdated = 0;
			bool mHasFiles = true;
			bool mObsolete = false;
		};

		struct FileInfo
		{
			uint32_t mId = 0;
			std::string mName;				// Like "my_mod_v2.zip"
			std::string mUrl;
			std::string mDescription;
			uint64_t mSize = 0;
			int mDownloads = 0;
			int64_t mDateAdded = 0;
			bool mFlagged = false;			// GameBanana's virus scan or file check didn't say "clean" / "ok"
		};

		struct ModDetails
		{
			uint32_t mId = 0;
			std::string mName;
			std::string mAuthor;
			std::string mCategory;
			std::string mVersion;
			std::string mSummary;			// The one-line description
			std::string mText;				// The full description, as plain text
			int mLikes = 0;
			int mViews = 0;
			int mDownloads = 0;
			int64_t mDateAdded = 0;
			int64_t mDateUpdated = 0;
			bool mObsolete = false;
			std::vector<FileInfo> mFiles;
			std::vector<std::string> mImageUrls;	// Preview pictures (220 pixels wide)
		};

		enum class Load
		{
			IDLE,
			LOADING,
			READY,
			FAILED
		};

		enum class Install
		{
			IDLE,
			DOWNLOADING,
			UNPACKING,
			DONE			// See mInstallSuccess and mInstallMessage; "acknowledgeInstall" goes back to IDLE
		};

		struct State
		{
			// The list of mods
			Load mListLoad = Load::IDLE;
			std::string mListError;
			Listing mListing = Listing::POPULAR;
			std::string mSearch;
			int mPage = 1;
			int mTotalMods = 0;				// In the whole listing (all pages)
			int mPerPage = 15;
			std::vector<ModSummary> mMods;

			// One mod's details
			Load mDetailsLoad = Load::IDLE;
			std::string mDetailsError;
			ModDetails mDetails;

			// Installing
			Install mInstall = Install::IDLE;
			std::string mInstallName;
			uint64_t mBytes = 0;
			uint64_t mTotalBytes = 0;
			int mFilesDone = 0;
			int mFilesTotal = 0;
			bool mInstallSuccess = false;
			std::string mInstallMessage;
			std::vector<std::string> mInstallWarnings;
		};

		struct Thumbnail
		{
			std::string mUrl;
			bool mFailed = false;			// Couldn't load or decode it (like a WebP picture)
			int mWidth = 0;
			int mHeight = 0;
			std::vector<uint32_t> mPixels;	// RGBA, one byte each, R in the lowest one (like rmx::Bitmap)
		};

		// A mod installed from here: noted in a little file in its folder ("3ds_gamebanana.txt"), so the browser can tell it's there
		// and whether GameBanana has a newer version
		struct InstalledMod
		{
			uint32_t mModId = 0;
			uint32_t mFileId = 0;
			int64_t mModUpdated = 0;		// The mod's "updated" date on GameBanana when it was installed
			std::string mFolder;
		};

		// The mods folder and a folder for temporary files (both ending with '/'), then the thread waits for requests
		void start(const std::string& modsDirectory, const std::string& workDirectory);
		void stop();		// Cancels what's running, waits for the thread to end

		State getState();

		void requestList(Listing listing, const std::string& search, int page);		// Replaces a list request that's still waiting
		void requestDetails(uint32_t modId);
		void requestThumbnail(const std::string& url, int maxWidth);
		void clearThumbnailRequests();
		bool takeThumbnail(Thumbnail& outThumbnail);

		void requestInstall(const FileInfo& file, const std::string& modName, uint32_t modId = 0, int64_t modUpdated = 0);
		bool getInstalledMod(uint32_t modId, InstalledMod& outMod);		// Once the thread has looked at the mods folder
		void cancelInstall();
		void acknowledgeInstall();
		bool takeInstalledMod(std::string& outFolder);		// Once for each successful install, for the game to turn it on

		// The pieces, also used by the host test
		void setBaseUrl(const std::string& baseUrl);		// Default "https://gamebanana.com"; the host and emulator tests use a local server
		std::string getListUrl(Listing listing, const std::string& search, int page);
		std::string getDetailsUrl(uint32_t modId);
		bool parseModList(const std::string& json, std::vector<ModSummary>& outMods, int& outTotal, std::string& outError);
		bool parseModDetails(const std::string& json, ModDetails& outDetails, std::string& outError);
		std::string htmlToText(const std::string& html);
		std::string toDisplayText(const std::string& utf8);		// Only characters the game's fonts have
		const char* getFileProblem(const FileInfo& file);		// Why the 3DS can't install this file (like a .rar), or nullptr
		bool decodeJpeg(const std::string& data, int maxWidth, Thumbnail& outThumbnail);
		bool readInstalledNote(const std::string& modFolderPath, InstalledMod& outMod);
		bool writeInstalledNote(const std::string& modFolderPath, const InstalledMod& mod);
	}
}

#endif
