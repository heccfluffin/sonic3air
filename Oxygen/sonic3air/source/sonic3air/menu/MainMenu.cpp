/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"
#include "sonic3air/menu/MainMenu.h"
#include "sonic3air/menu/GameApp.h"
#include "sonic3air/menu/MenuBackground.h"
#include "sonic3air/menu/SharedResources.h"
#include "sonic3air/audio/AudioOut.h"
#include "sonic3air/data/SharedDatabase.h"
#include "sonic3air/ConfigurationImpl.h"
#include "sonic3air/Game.h"
#include "sonic3air/version.inc"

#if defined(PLATFORM_3DS)
	#include "oxygen/platform/3ds/N3DSChallenges.h"
	#include "oxygen/platform/3ds/N3DSShop.h"
#endif
#include "oxygen/application/Application.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/application/gameview/GameView.h"
#include "oxygen/application/input/InputManager.h"
#include "oxygen/application/modding/ModManager.h"
#include "oxygen/helper/Utils.h"


namespace mainmenu
{
	enum Entry
	{
		NORMAL_GAME	= 0x01,
		ACT_SELECT	= 0x02,
		TIME_ATTACK	= 0x03,
		OPTIONS		= 0x10,
		EXTRAS		= 0x11,
		MODS		= 0x12,
		MENU_3DS	= 0x13,		// 3DS only: Play Coin shop
		EXIT		= 0xff
	};
}


MainMenu::MainMenu(MenuBackground& menuBackground) :
	mMenuBackground(&menuBackground)
{
	buildMenuEntries();
}

void MainMenu::buildMenuEntries()
{
	// Build up menu structure
	{
		mMenuEntries.clear();
		mMenuEntries.reserve(8);
		mMenuEntries.addEntry("NORMAL GAME", mainmenu::NORMAL_GAME);
		mMenuEntries.addEntry("ACT SELECT",  mainmenu::ACT_SELECT);
		mMenuEntries.addEntry("TIME ATTACK", mainmenu::TIME_ATTACK);
		mMenuEntries.addEntry("OPTIONS",	 mainmenu::OPTIONS);
		mMenuEntries.addEntry("EXTRAS",		 mainmenu::EXTRAS);
		mMenuEntries.addEntry("MODS",		 mainmenu::MODS);

	#if defined(PLATFORM_3DS)
		// Can be hidden in the options
		mShowing3DSMenu = n3ds::shop::get().mShowMenu;
		if (mShowing3DSMenu)
			mMenuEntries.addEntry("3DS",	 mainmenu::MENU_3DS);
	#endif

	#if !defined(PLATFORM_ANDROID) && !defined(PLATFORM_IOS) && !defined(PLATFORM_WEB)
		mMenuEntries.addEntry("EXIT",		 mainmenu::EXIT);
	#endif
	}

	// Set defaults
	mMenuEntries.mSelectedEntryIndex = 0;
}

MainMenu::~MainMenu()
{
}

GameMenuBase::BaseState MainMenu::getBaseState() const
{
	switch (mState)
	{
		case State::APPEAR:				 return BaseState::FADE_IN;
		case State::SHOW:				 return BaseState::SHOW;
		case State::FADE_TO_TITLESCREEN: return BaseState::FADE_OUT;
		case State::FADE_TO_DATASELECT:	 return BaseState::FADE_OUT;
		case State::FADE_TO_SUBMENU:	 return BaseState::FADE_OUT;
		default:						 return BaseState::INACTIVE;
	}
}

void MainMenu::setBaseState(BaseState baseState)
{
	switch (baseState)
	{
		case BaseState::INACTIVE: mState = State::INACTIVE;  break;
		case BaseState::FADE_IN:  mState = State::APPEAR;  break;
		case BaseState::SHOW:	  mState = State::SHOW;  break;
		case BaseState::FADE_OUT: mState = State::FADE_TO_EXIT;  break;
	}
}

void MainMenu::onFadeIn()
{
	mState = State::APPEAR;

#if defined(PLATFORM_3DS)
	// The "3DS" entry may have been switched on or off in the options
	if (mShowing3DSMenu != n3ds::shop::get().mShowMenu)
	{
		const uint32 selectedData = mMenuEntries.selected().mData;
		buildMenuEntries();
		mMenuEntries.mSelectedEntryIndex = 0;
		for (size_t i = 0; i < mMenuEntries.size(); ++i)
		{
			if (mMenuEntries[i].mData == selectedData)
				mMenuEntries.mSelectedEntryIndex = (int)i;
		}
	}
#endif

	mMenuBackground->showPreview(false);
	mMenuBackground->startTransition(MenuBackground::Target::SPLIT);

	AudioOut::instance().stopSoundContext(AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_MUSIC);

	// Play "Data Select" music inside this menu
	AudioOut::instance().setMenuMusic(0x2f);

	for (size_t i = 0; i < mMenuEntries.size(); ++i)
	{
		mMenuEntries[i].mAnimation.mVisibility = ((int)i == mMenuEntries.mSelectedEntryIndex) ? 1.0f : 0.0f;
	}

	// Check for unlocked secrets (needed when new game versions added secrets or reduced requirements)
	Game::instance().checkForUnlockedSecrets();
}

bool MainMenu::canBeRemoved()
{
	return (mState == State::INACTIVE && mVisibility <= 0.0f);
}

void MainMenu::initialize()
{
}

void MainMenu::deinitialize()
{
}

void MainMenu::keyboard(const rmx::KeyboardEvent& ev)
{
}

void MainMenu::update(float timeElapsed)
{
	GameMenuBase::update(timeElapsed);

	// Don't react to input during transitions
	if (mState == State::SHOW)
	{
		// Update menu entries
		const GameMenuEntries::UpdateResult result = mMenuEntries.update();
		if (result != GameMenuEntries::UpdateResult::NONE)
		{
			playMenuSound(0x5b);
		}

		if (!FTX::keyState(SDLK_LALT) && !FTX::keyState(SDLK_RALT))
		{
			const InputManager::ControllerScheme& keys = InputManager::instance().getController(0);
			const uint32 selectedData = mMenuEntries.selected().mData;

			if (keys.Start.justPressed() || keys.A.justPressed() || keys.X.justPressed())
			{
				switch (selectedData)
				{
					case mainmenu::NORMAL_GAME:
						triggerStartNormalGame();
						break;

					case mainmenu::ACT_SELECT:
						triggerStartActSelect();
						break;

					case mainmenu::TIME_ATTACK:
						openTimeAttack();
						break;

					case mainmenu::OPTIONS:
						openOptions();
						break;

					case mainmenu::EXTRAS:
						openExtras();
						break;

					case mainmenu::MODS:
						openMods();
						break;

				#if defined(PLATFORM_3DS)
					case mainmenu::MENU_3DS:
						open3DSMenu();
						break;
				#endif

					case mainmenu::EXIT:
						exitGame();
						break;
				}
			}
			else if (keys.B.justPressed() || keys.Back.justPressed())
			{
				mState = State::FADE_TO_TITLESCREEN;
				GameApp::instance().getGameView().startFadingOut();
			}
		}
	}

	// Update animation
	{
		const float maxStep = timeElapsed * 10.0f;
		for (size_t i = 0; i < mMenuEntries.size(); ++i)
		{
			const float target = ((int)i == mMenuEntries.mSelectedEntryIndex) ? 1.0f : 0.0f;
			mMenuEntries[i].mAnimation.mVisibility += clamp(target - mMenuEntries[i].mAnimation.mVisibility, -maxStep, maxStep);
		}
	}

	if (mState == State::APPEAR)
	{
		if (updateFadeIn(timeElapsed * 4.0f))
		{
			mState = State::SHOW;
		}
	}
	else if (mState > State::SHOW)
	{
		if (updateFadeOut(timeElapsed * 4.0f))
		{
			switch (mState)
			{
				case State::FADE_TO_TITLESCREEN:
					mMenuBackground->setGameStartedMenu();
					GameApp::instance().openTitleScreen();
					break;

				case State::FADE_TO_DATASELECT:
					startNormalGame();
					break;

				case State::FADE_TO_ACTSELECT:
					openActSelectMenu();
					break;

				case State::FADE_TO_EXIT:
					FTX::System->quit();
					break;

				default:
					break;
			}
			mState = State::INACTIVE;
		}
	}
}

void MainMenu::render()
{
	GuiBase::render();

	Drawer& drawer = EngineMain::instance().getDrawer();

	const int positionYDefault[] = { 31, 58, 85,  125, 146, 167,  195 };
	const int positionYWith3DS[] = { 31, 58, 85,  125, 144, 163, 182,  206 };		// 3DS: one more entry
	const int* positionY = (mMenuEntries.size() > 7) ? positionYWith3DS : positionYDefault;
	for (size_t line = 0; line < mMenuEntries.size() && line < 8; ++line)
	{
		bool isMainEntry = (line < 3);

		const auto& entry = mMenuEntries[line];
		const std::string& text = entry.mOptions.empty() ? entry.mText : entry.mOptions[entry.mSelectedIndex].mText;

		const int lineOffset = (mState < State::SHOW) ? (int)(mMenuEntries.size() - 1 - line) : (int)line;
		const int px = (isMainEntry ? 218 : 232) + roundToInt(saturate(1.0f - mVisibility - lineOffset * 0.1f) * 200.0f - mMenuEntries[line].mAnimation.mVisibility * 10.0f);
		const int py = positionY[line];
		const bool isSelected = ((int)line == mMenuEntries.mSelectedEntryIndex);
		const bool isSelectable = entry.isFullyInteractable();
		const Color colorBG = isSelectable ? (isSelected ? Color(1.0f, 1.0f, 0.5f, mVisibility) : Color(0.5f, 0.75f, 1.0f, mVisibility)) : Color(0.4f, 0.4f, 0.4f, mVisibility * 0.25f);
		const Color color   = isSelectable ? (isSelected ? Color(1.0f, 1.0f, 0.0f, mVisibility) : Color(1.0f, 1.0f, 1.0f, mVisibility)) : Color(0.4f, 0.4f, 0.4f, mVisibility * 0.75f);

		const int underlineOffset = isMainEntry ? 15 : 9;
		drawer.drawRect(Recti(px-6, py+underlineOffset+1, 250, 3), Color(0.0f, 0.0f, 0.0f, colorBG.a * mVisibility));	// Squared alpha for the shadow is intentional
		drawer.drawRect(Recti(px-7, py+underlineOffset, 250, 3), colorBG);

		if (isMainEntry)
		{
			drawer.printText(global::mSonicFontC, Recti(px, py, 200, 18), text, 4, color);
		}
		else
		{
			DrawerPrintOptions printOptions;
			printOptions.mAlignment = 4;
			printOptions.mTintColor = color;
			printOptions.mSpacing = 1;
			drawer.printText(global::mOxyfontRegular, Recti(px, py, 200, 10), text, printOptions);
		}
	}

#if defined(PLATFORM_3DS)
	// The daily challenge pinned in the 3DS menu: a little card down in the water on the left, under the island
	{
		const n3ds::challenges::Data& challenges = n3ds::challenges::get();
		if (challenges.mPinned >= 0 && challenges.mPinned < challenges.mCount && !challenges.mChallenges[challenges.mPinned].mDone)
		{
			const n3ds::challenges::Challenge& challenge = challenges.mChallenges[challenges.mPinned];
			const float alpha = mVisibility;
			const Recti card(6 - roundToInt((1.0f - mVisibility) * 200.0f), 181, 174, 46);		// The game's picture ends a few pixels above the screen's bottom
			drawer.drawRect(Recti(card.x - 1, card.y - 1, card.width + 2, card.height + 2), Color(0.9f, 0.25f, 0.2f, alpha));
			drawer.drawRect(card, Color(0.16f, 0.04f, 0.05f, alpha * 0.9f));

			// A pushpin in the corner, and which one it is
			drawer.drawRect(Recti(card.x + card.width - 9, card.y - 3, 1, 4), Color(0.75f, 0.75f, 0.8f, alpha));
			drawer.drawRect(Recti(card.x + card.width - 11, card.y - 6, 5, 4), Color(0.9f, 0.15f, 0.15f, alpha));
			String slot(n3ds::challenges::getSlotName(challenges.mPinned));
			slot.upperCase();
			const std::string title = (challenges.mPinned == 4) ? (slot.toStdString() + String(0, " - %d XP", n3ds::challenges::getXpFor(challenges.mPinned)).toStdString())
															   : ("ROBOTNIK'S " + slot.toStdString() + String(0, " - %d XP", n3ds::challenges::getXpFor(challenges.mPinned)).toStdString());
			drawer.printText(global::mOxyfontTiny, Recti(card.x + 5, card.y + 3, 0, 10), title, 1, Color(1.0f, 0.85f, 0.2f, alpha));

			std::vector<std::string_view> lines;
			const std::string description = n3ds::challenges::describe(challenge);
			utils::splitTextIntoLines(lines, description, global::mOxyfontTiny, card.width - 10);
			for (size_t k = 0; k < lines.size() && k < 2; ++k)
				drawer.printText(global::mOxyfontTiny, Recti(card.x + 5, card.y + 14 + (int)k * 10, 0, 10), lines[k], 1, Color(1.0f, 1.0f, 1.0f, alpha));
			const std::string progress = n3ds::challenges::describeProgress(challenge);
			if (!progress.empty())
				drawer.printText(global::mOxyfontTiny, Recti(card.x + card.width - 5, card.y + 34, 0, 10), progress, 3, Color(0.6f, 0.9f, 1.0f, alpha));
		}
	}
#endif

	// Version info
	{
		// Make place for FPS display if it's shown
		int px = (int)(mRect.width - ((Configuration::instance().mPerformanceDisplay != 0) ? 32 : 2) + (1.0f - mVisibility) * 100.0f);
	#if defined(PLATFORM_3DS) && defined(S3AIR_3DS_BUILD_ID)
		std::wstring text = L"\x02c5" BUILD_STRING L" " BUILD_VARIANT L" - 3DS " S3AIR_3DS_BUILD_ID;
	#else
		std::wstring text = L"\x02c5" BUILD_STRING L" " BUILD_VARIANT;
	#endif
		drawer.printText(global::mSmallfont, Vec2i(px, 1), text, 3, Color(0.2f, 0.2f, 0.2f, mVisibility * 0.3f));

		// Show whether dev mode is active, or one of the non-default glitch fix settings
		px -= 6 + global::mSmallfont.getWidth(text);
		if (EngineMain::getDelegate().useDeveloperFeatures())
		{
			drawer.printText(global::mSmallfont, Vec2i(px, 1), "DEV MODE", 3, Color(0.6f, 0.2f, 0.2f, mVisibility * 0.3f));
		}
		else
		{
			const uint32 value = ConfigurationImpl::instance().mActiveGameSettings->getValue((uint32)SharedDatabase::Setting::SETTING_FIX_GLITCHES);
			if (value < 2)
			{
				const char* txt = (value == 0) ? "NO GLITCH FIXES" : "ONLY BASIC FIXES";
				drawer.printText(global::mSmallfont, Vec2i(px, 1), txt, 3, Color(0.6f, 0.2f, 0.2f, mVisibility * 0.3f));
			}
		}
	}

	drawer.performRendering();
}

void MainMenu::triggerStartNormalGame()
{
	playMenuSound(0x63);
	mState = State::FADE_TO_DATASELECT;
	GameApp::instance().getGameView().startFadingOut();
}

void MainMenu::triggerStartActSelect()
{
	playMenuSound(0x63);
	mState = State::FADE_TO_ACTSELECT;
	GameApp::instance().getGameView().startFadingOut();
}

void MainMenu::startNormalGame()
{
	// Init simulation
	Game::instance().startIntoDataSelect();
	GameApp::instance().onStartGame();
	mMenuBackground->setGameStartedMenu();
}

void MainMenu::openActSelectMenu()
{
	// Init simulation
	Game::instance().startIntoActSelect();
	GameApp::instance().onStartGame();
	mMenuBackground->setGameStartedMenu();
}

void MainMenu::openTimeAttack()
{
	playMenuSound(0x63);
	mMenuBackground->openTimeAttackMenu();
	mState = State::FADE_TO_SUBMENU;
}

void MainMenu::openOptions()
{
	playMenuSound(0x63);
	mMenuBackground->openOptions();
	mState = State::FADE_TO_SUBMENU;
}

void MainMenu::openExtras()
{
	playMenuSound(0x63);
	mMenuBackground->openExtras();
	mState = State::FADE_TO_SUBMENU;
}

void MainMenu::openMods()
{
	playMenuSound(0x63);
	mMenuBackground->openMods();
	mState = State::FADE_TO_SUBMENU;
}

#if defined(PLATFORM_3DS)
void MainMenu::open3DSMenu()
{
	playMenuSound(0x63);
	mMenuBackground->open3DSMenu();
	mState = State::FADE_TO_SUBMENU;
}
#endif

void MainMenu::exitGame()
{
	AudioOut::instance().fadeOutChannel(0, 0.25f);
	GameApp::instance().getGameView().startFadingOut(0.25f);
	mMenuBackground->startTransition(MenuBackground::Target::TITLE);
	mState = State::FADE_TO_EXIT;
}
