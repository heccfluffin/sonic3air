/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"
#include "sonic3air/menu/mods/ModsStartPage.h"
#include "sonic3air/menu/SharedResources.h"

#include "oxygen/platform/PlatformFunctions.h"


namespace
{
	enum class Option
	{
		OPEN_MODS_FOLDER = 0xfff0,
		OPEN_MANUAL		 = 0xfff1,
		INSTALL_OVER_WIFI = 0xfff2,
		BROWSE_GAMEBANANA = 0xfff3,
		GO_BACK			 = 0xffff,
	};
}


void ModsStartPage::initialize()
{
	if (!mMenuEntries.empty())
		return;

#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_MAC) || defined(PLATFORM_LINUX)
	mMenuEntries.addEntry("Open mods " PLATFORM_DIRECTORY_STRING, (uint32)Option::OPEN_MODS_FOLDER);
#endif

#if defined(PLATFORM_3DS)
	mMenuEntries.addEntry("Browse mods on GameBanana", (uint32)Option::BROWSE_GAMEBANANA);
	mMenuEntries.addEntry("Install mods over Wi-Fi", (uint32)Option::INSTALL_OVER_WIFI);
	mMenuEntries.addEntry("Read the manual (on your phone)", (uint32)Option::OPEN_MANUAL);
#else
	mMenuEntries.addEntry("Open Manual in web browser", (uint32)Option::OPEN_MANUAL);
#endif
	mMenuEntries.addEntry("Back", (uint32)Option::GO_BACK);
}

bool ModsStartPage::update(float timeElapsed)
{
	const InputManager::ControllerScheme& keys = InputManager::instance().getController(0);

	enum class ButtonEffect
	{
		NONE,
		ACCEPT,
		BACK
	};
	const ButtonEffect buttonEffect = (keys.Start.justPressed() || keys.A.justPressed()) ? ButtonEffect::ACCEPT :
									  (keys.B.justPressed() || keys.Back.justPressed()) ? ButtonEffect::BACK : ButtonEffect::NONE;

	switch (buttonEffect)
	{
		case ButtonEffect::ACCEPT:
		{
			// Accept current selection
			if (mMenuEntries.hasSelected())
			{
				switch ((Option)mMenuEntries.selected().mData)
				{
					case Option::OPEN_MODS_FOLDER:
						GameMenuBase::playMenuSound(0x63);
						PlatformFunctions::openDirectoryExternal(Configuration::instance().mAppDataPath + L"mods/");
						break;

				#if defined(PLATFORM_3DS)
					case Option::INSTALL_OVER_WIFI:
						GameMenuBase::playMenuSound(0x63);
						mWifiRequested = true;
						break;

					case Option::BROWSE_GAMEBANANA:
						GameMenuBase::playMenuSound(0x63);
						mGameBananaRequested = true;
						break;
				#endif

					case Option::OPEN_MANUAL:
						GameMenuBase::playMenuSound(0x63);
						PlatformFunctions::openURLExternal("https://sonic3air.org/Manual.pdf");
						break;

					case Option::GO_BACK:
						return false;
				}
			}
			break;
		}

		case ButtonEffect::BACK:
			return false;

		case ButtonEffect::NONE:
			break;
	}

	const GameMenuEntries::UpdateResult result = mMenuEntries.update();
	if (result == GameMenuEntries::UpdateResult::ENTRY_CHANGED)
	{
		GameMenuBase::playMenuSound(0x5b);
	}
	return true;
}

void ModsStartPage::render(Drawer& drawer, float visibility)
{
	const int globalOffsetX = -roundToInt(saturate(1.0f - visibility) * 300.0f);

	Recti rect(globalOffsetX + 32, 16, 300, 18);
	drawer.printText(global::mSonicFontB, rect, "No mods installed!", 1, Color(0.6f, 0.8f, 1.0f, visibility));
	rect.y += 22;

	Color color(0.8f, 0.9f, 1.0f, visibility);
#if defined(PLATFORM_3DS)
	drawer.printText(global::mOxyfontSmall, rect, "Browse the mods on GameBanana and install them right here,", 1, color);
	rect.y += 14;
	drawer.printText(global::mOxyfontSmall, rect, "or send a mod's .zip from your phone or PC over Wi-Fi.", 1, color);
	rect.y += 18;
	drawer.printText(global::mOxyfontSmall, rect, "The mod patcher on your PC works too, with the SD card", 1, color);
	rect.y += 14;
	drawer.printText(global::mOxyfontSmall, rect, "in it (see the install guide).", 1, color);
	rect.y += 24;
#else
	drawer.printText(global::mOxyfontSmall, rect, "Have a look at the game manual PDF for instructions", 1, color);
	rect.y += 14;
	drawer.printText(global::mOxyfontSmall, rect, "on how to install mods.", 1, color);
	rect.y += 18;
	drawer.printText(global::mOxyfontSmall, rect, "Note that you will have to restart Sonic 3 A.I.R.", 1, color);
	rect.y += 14;
	drawer.printText(global::mOxyfontSmall, rect, "to make it scan for new mods.", 1, color);
	rect.y += 32;
#endif
	rect.x += 16;

	for (size_t index = 0; index < mMenuEntries.size(); ++index)
	{
		const auto& entry = mMenuEntries[index];
		const bool isSelected = ((int)index == mMenuEntries.mSelectedEntryIndex);
		color = isSelected ? Color::YELLOW : Color::WHITE;
		color.a *= visibility;

		drawer.printText(global::mOxyfontRegular, rect, entry.mText, 1, color);
		rect.y += 22;
	}
}
