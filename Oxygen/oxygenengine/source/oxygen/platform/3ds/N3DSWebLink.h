/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: links to websites, which the 3DS can't open (see "openURLExternal"), are shown by the game instead.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include <string>


namespace n3ds
{
	namespace weblink
	{
		// A menu wants to open this website
		void request(const std::string& url);

		// For the game to show it (once): true with the address if there's a new one
		bool takeRequest(std::string& outUrl);
	}
}

#endif
