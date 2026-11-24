/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: installing mods over Wi-Fi. While the Mods menu shows it, the game serves a little web page (on port 80,
*	or 8080 if that's taken): open the 3DS's address in a browser on a phone or PC in the same Wi-Fi, pick a mod's .zip, and it
*	gets uploaded straight to the SD card and unpacked into the mods folder - with the same rules as the PC mod patcher
*	(the folder with mod.json in it, even inside an extra folder; unsafe paths refused; warnings for shaders and big files).
*	The game itself then turns the new mod on (see ModsMenu).
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
	namespace modserver
	{
		struct Status
		{
			enum class State
			{
				STOPPED,
				NO_WIFI,		// The 3DS isn't connected (wireless off, or no network)
				READY,			// Waiting for an upload
				RECEIVING,
				UNPACKING
			};

			State mState = State::STOPPED;
			std::string mAddress;			// Like "http://192.168.1.23/"
			std::string mFileName;			// The upload, while receiving or unpacking it
			uint64_t mBytes = 0;
			uint64_t mTotalBytes = 0;
			int mFilesDone = 0;
			int mFilesTotal = 0;

			int mInstalls = 0;				// Finished installs since the start
			bool mLastSuccess = false;
			std::string mLastMessage;		// What happened with the latest upload
			std::vector<std::string> mLastWarnings;
		};

		// The mods folder, and a folder for the upload and unpacking (on the same SD card, so the result can be moved in place)
		bool start(const std::string& modsDirectory, const std::string& workDirectory, uint32_t ipAddress);
		void stop();
		bool isRunning();

		Status getStatus();

		// A mod that got installed: its folder name in the mods folder (once each)
		bool takeInstalledMod(std::string& outFolder);

		// The mods for the web page's list, as JSON: [{"name": "...", "folder": "...", "active": true}, ...]
		void setInstalledModsJson(const std::string& json);
		std::string escapeJson(const std::string& text);

		// The checks on unpacking, also used by the host test: the zip's entries (paths with '/'), which of them is the mod's
		// root folder ("" or like "Wrapper/"), and the folder name it gets
		bool findModRoot(const std::vector<std::string>& entries, std::string& outRoot, std::string& outError);
		std::string cleanFolderName(const std::string& name);
		bool isSafeZipPath(const std::string& path);
	}
}

#endif
