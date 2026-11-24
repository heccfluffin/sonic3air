/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "sonic3air/menu/GameMenuBase.h"


class ModsStartPage
{
public:
	void initialize();
	bool update(float timeElapsed);
	void render(Drawer& drawer, float visibility);

#if defined(PLATFORM_3DS)
	bool mWifiRequested = false;		// "Install mods over Wi-Fi" was chosen
#endif

private:
	GameMenuEntries mMenuEntries;
};
