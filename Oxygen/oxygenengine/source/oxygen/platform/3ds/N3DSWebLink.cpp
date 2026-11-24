/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: links to websites, shown by the game instead of opened.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSWebLink.h"


namespace n3ds
{
	namespace weblink
	{
		namespace
		{
			std::string gRequestedUrl;
		}

		void request(const std::string& url)
		{
			gRequestedUrl = url;
			RMX_LOG_INFO("Web link (shown, the 3DS can't open it): " << url);
		}

		bool takeRequest(std::string& outUrl)
		{
			if (gRequestedUrl.empty())
				return false;
			outUrl.swap(gRequestedUrl);
			gRequestedUrl.clear();
			return true;
		}
	}
}

#endif
