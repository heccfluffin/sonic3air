/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: QR codes for short texts like web addresses (to scan with a phone), made on the console.
*	Byte mode, error correction level M (or L when that's needed to fit), versions 1 to 6: up to 106 bytes.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include <string>
#include <vector>


namespace n3ds
{
	namespace qrcode
	{
		struct Code
		{
			int mSize = 0;					// Modules per side (0 when the text didn't fit)
			std::vector<bool> mModules;		// Row by row, true = dark; no quiet zone around it

			inline bool get(int x, int y) const  { return mModules[(size_t)(y * mSize + x)]; }
		};

		Code encode(const std::string& text);
	}
}
