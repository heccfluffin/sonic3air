/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the bottom screen - a controls overview with touch buttons for a quick save state,
*	or the profiler's statistics.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include <string>


namespace n3ds
{
	namespace bottomscreen
	{
		enum class Request
		{
			NONE,
			SAVE_STATE,
			LOAD_STATE
		};

		enum class Result
		{
			SAVED,
			LOADED,
			NO_STATE,		// Loading: there's no saved state yet
			FAILED,
			SCREENSHOT_SAVED
		};

		// Call once per frame on the main thread. "saveStatesAllowed" tells whether save states can be used right now
		// (they get greyed out otherwise), "slot" which save file is being played (0 = "No Save", 1-8) and "slotHasState" whether
		// it has a save state. Returns what the player tapped; the caller performs it, then reports back with "showResult".
		Request update(bool saveStatesAllowed, int slot, bool slotHasState);
		void showResult(Result result);

		// Switches to another bottom screen theme (see n3ds::shop::Theme), also for previews in the "3DS" menu
		void setTheme(int theme);
		void setCustomTheme(const std::string& name);	// The player's own picture (file name in the themes folder), also for previews

		// Whether the bottom screen shows the profiler's statistics (otherwise the profiler only writes its log file)
		bool isShowingStats();
	}
}
