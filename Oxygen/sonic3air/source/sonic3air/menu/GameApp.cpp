/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"
#include "sonic3air/menu/GameApp.h"
#include "sonic3air/menu/GameMenuManager.h"
#include "sonic3air/menu/MenuBackground.h"
#include "sonic3air/menu/PauseMenu.h"
#include "sonic3air/menu/SharedResources.h"
#include "sonic3air/menu/TimeAttackResultsMenu.h"
#include "sonic3air/menu/context/ApplicationContextMenu.h"
#include "sonic3air/menu/overlays/SecretUnlockedWindow.h"
#include "sonic3air/menu/overlays/SkippableCutsceneWindow.h"
#include "sonic3air/audio/AudioOut.h"
#include "sonic3air/data/SharedDatabase.h"
#include "sonic3air/Game.h"

#include "oxygen/application/Application.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/application/gameview/GameView.h"
#include "oxygen/application/input/ControlsIn.h"
#include "oxygen/application/input/InputManager.h"
#include "oxygen/application/video/VideoOut.h"
#include "oxygen/helper/FileHelper.h"
#include "oxygen/menu/imgui/ImGuiManager.h"
#include "oxygen/menu/imgui/implementations/ImGuiFileBrowser.h"
#include "oxygen/platform/PlatformFunctions.h"
#include "oxygen/rendering/utils/RenderUtils.h"
#include "oxygen/simulation/EmulatorInterface.h"
#include "oxygen/simulation/Simulation.h"
#if defined(PLATFORM_3DS)
	#include "oxygen/platform/3ds/N3DSBottomScreen.h"
	#include "oxygen/platform/3ds/N3DSFileWriter.h"
	#include "oxygen/platform/3ds/N3DSNotificationLight.h"
	#include "oxygen/platform/3ds/N3DSScreenshot.h"
	#include "oxygen/platform/3ds/N3DSJournal.h"
	#include "sonic3air/version.inc"
	#include "oxygen/rendering/parts/RenderParts.h"
	#include "oxygen/simulation/CodeExec.h"
	#include "oxygen/simulation/LemonScriptRuntime.h"
	#include "oxygen/simulation/SaveStateSerializer.h"
	#include <lemon/runtime/Runtime.h>
	#include "sonic3air/data/N3DSAmbush.h"
	#include "sonic3air/menu/GameMenuBase.h"
	#include "oxygen/platform/3ds/N3DSQRCode.h"
	#include "oxygen/platform/3ds/N3DSWebLink.h"
	#include <3ds.h>
	#include <memory>
	#include <sys/stat.h>
	#include <unistd.h>
#endif


namespace
{
	static const constexpr float DISCLAIMER_FADE_IN_TIME  = 0.8f;
	static const constexpr float DISCLAIMER_FADE_OUT_TIME = 0.2f;
}


GameApp::GameApp() :
	mGameMenuManager(new GameMenuManager()),
	mMenuBackground(new MenuBackground()),
	mPauseMenu(new PauseMenu()),
	mTimeAttackResultsMenu(new TimeAttackResultsMenu())
{
	mGameMenuManager->initWithRoot(*mMenuBackground);
}

GameApp::~GameApp()
{
	delete mGameMenuManager;
	delete mMenuBackground;
	delete mPauseMenu;
	delete mTimeAttackResultsMenu;
	delete mSecretUnlockedWindow;
	delete mSkippableCutsceneWindow;
}

void GameApp::initialize()
{
	// Init shared resources
	global::loadSharedResources();

	mGameView = &Application::instance().getGameView();
	Simulation& simulation = Application::instance().getSimulation();
	simulation.setRunning(false);

	gotoPhase(Configuration::instance().mStartPhase);
	if (Configuration::instance().mLoadLevel >= 0 && Configuration::instance().mLoadLevel <= 0xffff)
	{
		Game::instance().startIntoLevel(Game::Mode::UNDEFINED, 0, Configuration::instance().mLoadLevel, Configuration::instance().mUseCharacters);
	}

	if (nullptr == mApplicationContextMenu)
	{
		mApplicationContextMenu = &createChild<ApplicationContextMenu>();
	}

#if defined(PLATFORM_3DS)
	mMenuBackground->preload3DSMenu();
#endif
}

void GameApp::deinitialize()
{
	// Remove children that get explicitly deleted
	if (nullptr != mMenuBackground)
		mGameView->removeChild(*mMenuBackground);
	if (nullptr != mPauseMenu)
		mGameView->removeChild(*mPauseMenu);
	if (nullptr != mTimeAttackResultsMenu)
		mGameView->removeChild(*mTimeAttackResultsMenu);
	if (nullptr != mSecretUnlockedWindow)
		mGameView->removeChild(*mSecretUnlockedWindow);
	if (nullptr != mSkippableCutsceneWindow)
		mGameView->removeChild(*mSkippableCutsceneWindow);
}

void GameApp::mouse(const rmx::MouseEvent& ev)
{
	GuiBase::mouse(ev);
}

void GameApp::keyboard(const rmx::KeyboardEvent& ev)
{
	GuiBase::keyboard(ev);
}

void GameApp::update(float timeElapsed)
{
#if defined(PLATFORM_3DS)
	// A website shown: the menus wait until it's closed
	if (updateWebLink(timeElapsed))
	{
		updateBottomScreen();
		return;
	}
#endif

	GuiBase::update(timeElapsed);

	// Update disclaimer fade-in / fade-out
	{
		const float dt = std::min(timeElapsed, 0.05f);
		if (mCurrentState == State::DISCLAIMER)
		{
			mDisclaimerVisibility = std::min(mDisclaimerVisibility + dt / DISCLAIMER_FADE_IN_TIME, 1.0f);

			mStateTimeout -= dt;
			if (mStateTimeout <= 0.0f || InputManager::instance().anythingPressed())
			{
				gotoPhase(1);
			}
		}
		else
		{
			if (mDisclaimerVisibility > 0.0f)
			{
				mDisclaimerVisibility -= dt / DISCLAIMER_FADE_OUT_TIME;
				if (mDisclaimerVisibility <= 0.0f)
				{
					mDisclaimerVisibility = 0.0f;
					mDisclaimerTexture.clearBitmap();	// Unload to save on RAM
				}
			}
		}
	}

	if (mCurrentState == State::INGAME)
	{
		// Input
		Game::instance().updateSpecialInput(timeElapsed);
	}

#if defined(PLATFORM_3DS)
	updateBottomScreen();
#endif

	// GUI
	mGameMenuManager->updateMenus();

	if (nullptr != mPauseMenu->getParent() && mPauseMenu->canBeRemoved())
	{
		mGameView->removeChild(*mPauseMenu);
	}

	if (nullptr != mRemoveChild && mRemoveChild->getParent() == mGameView)
	{
		mGameView->removeChild(*mRemoveChild);
		mRemoveChild = nullptr;
	}

	// Make sure the overlay windows are always on top
	if (nullptr != mSecretUnlockedWindow && nullptr != mSecretUnlockedWindow->getParent())
	{
		mSecretUnlockedWindow->getParent()->moveToFront(*mSecretUnlockedWindow);
	}
	if (nullptr != mSkippableCutsceneWindow && nullptr != mSkippableCutsceneWindow->getParent())
	{
		if (mSkippableCutsceneWindow->canBeRemoved())
		{
			mGameView->removeChild(*mSkippableCutsceneWindow);
		}
		else
		{
			mSkippableCutsceneWindow->getParent()->moveToFront(*mSkippableCutsceneWindow);
		}
	}
}

void GameApp::render()
{
	Drawer& drawer = EngineMain::instance().getDrawer();

	if (mDisclaimerVisibility > 0.0f)
	{
		const Rectf rect = RenderUtils::getLetterBoxRect(FTX::screenRect(), (float)mDisclaimerTexture.getWidth() / (float)mDisclaimerTexture.getHeight());
		drawer.setBlendMode(BlendMode::OPAQUE);
		drawer.setSamplingMode(SamplingMode::BILINEAR);
		drawer.drawRect(rect, mDisclaimerTexture, Color(mDisclaimerVisibility, mDisclaimerVisibility, mDisclaimerVisibility));
		drawer.setSamplingMode(SamplingMode::POINT);
		drawer.performRendering();
	}

	GuiBase::render();

#if defined(PLATFORM_3DS)
	// 3DS: the build on the title screen (game mode 0x04, also during the SEGA logo and intro)
	if (mCurrentState == State::TITLE_SCREEN && EmulatorInterface::instance().readMemory8(0xfffff600) == 0x04)
	{
	#if defined(S3AIR_3DS_BUILD_ID)
		const char* text = "3DS port " S3AIR_3DS_BUILD_ID "  -  A.I.R. " BUILD_STRING;
	#else
		const char* text = "3DS port  -  A.I.R. " BUILD_STRING;
	#endif
		drawer.printText(global::mSmallfontSemiOutlined, Vec2i(3, (int)mRect.height - 8), text, 1, Color(1.0f, 1.0f, 1.0f, 0.75f));
		drawer.performRendering();
	}

	// The shadow ambush's WARNING (and the message when he was outrun), over the game
	if (mCurrentState == State::INGAME)
	{
		n3ds::ambush::render(drawer, (int)FTX::screenRect().width);
		drawer.performRendering();
	}

	renderWebLink(drawer);
#endif
}

#if defined(PLATFORM_3DS)
bool GameApp::updateWebLink(float timeElapsed)
{
	std::string url;
	if (n3ds::weblink::takeRequest(url))
	{
		mWebLinkUrl = url;
		mWebLinkTime = 0.0f;
	}
	if (mWebLinkUrl.empty())
		return false;

	mWebLinkTime += timeElapsed;
	const InputManager::ControllerScheme& keys = InputManager::instance().getController(0);
	if (mWebLinkTime > 0.2f && (keys.A.justPressed() || keys.B.justPressed() || keys.Start.justPressed() || keys.Back.justPressed()))
	{
		mWebLinkUrl.clear();
		GameMenuBase::playMenuSound(0xad);
	}
	return true;
}

void GameApp::renderWebLink(Drawer& drawer)
{
	if (mWebLinkUrl.empty())
		return;

	// The 3DS can't open websites (and its browser doesn't show PDFs like the manual): a QR code to scan with a phone, and the address
	const Recti screen(0, 0, (int)FTX::screenRect().width, (int)FTX::screenRect().height);
	const float alpha = std::min(mWebLinkTime / 0.15f, 1.0f);
	drawer.drawRect(Rectf(screen), Color(0.0f, 0.0f, 0.0f, 0.6f * alpha));

	static std::string qrUrl;
	static n3ds::qrcode::Code qrCode;
	if (qrUrl != mWebLinkUrl)
	{
		qrUrl = mWebLinkUrl;
		qrCode = n3ds::qrcode::encode(mWebLinkUrl);
	}
	const n3ds::qrcode::Code* code = (qrCode.mSize > 0) ? &qrCode : nullptr;

	const int width = std::min(screen.width - 16, 340);
	const int height = 150;
	const Recti panel((screen.width - width) / 2, (screen.height - height) / 2, width, height);
	drawer.drawRect(Rectf(panel), Color(0.07f, 0.10f, 0.22f, alpha));
	drawer.drawRect(Rectf((float)panel.x, (float)panel.y, (float)panel.width, 2.0f), Color(0.4f, 0.6f, 1.0f, alpha));
	drawer.drawRect(Rectf((float)panel.x, (float)(panel.y + panel.height - 2), (float)panel.width, 2.0f), Color(0.4f, 0.6f, 1.0f, alpha));

	int textX = panel.x + 14;
	if (nullptr != code)
	{
		// Dark modules in runs per row, on white with a quiet zone of two modules
		const int module = 3;
		const int quiet = 2 * module;
		const int size = code->mSize * module + quiet * 2;
		const int qx = panel.x + 12;
		const int qy = panel.y + (panel.height - size) / 2;
		drawer.drawRect(Rectf((float)qx, (float)qy, (float)size, (float)size), Color(1.0f, 1.0f, 1.0f, alpha));
		for (int row = 0; row < code->mSize; ++row)
		{
			for (int column = 0; column < code->mSize; )
			{
				if (!code->get(column, row))
				{
					++column;
					continue;
				}
				const int start = column;
				while (column < code->mSize && code->get(column, row))
					++column;
				drawer.drawRect(Rectf((float)(qx + quiet + start * module), (float)(qy + quiet + row * module), (float)((column - start) * module), (float)module), Color(0.0f, 0.0f, 0.0f, alpha));
			}
		}
		textX = qx + size + 12;
	}

	const Color white(1.0f, 1.0f, 1.0f, alpha);
	const Color blue(0.6f, 0.8f, 1.0f, alpha);
	int y = panel.y + 26;
	drawer.printText(global::mOxyfontRegular, Recti(textX, y, 0, 10), "Open this on your", 1, white);
	drawer.printText(global::mOxyfontRegular, Recti(textX, y + 14, 0, 10), "phone or computer:", 1, white);
	y += 40;
	if (nullptr != code)
	{
		drawer.printText(global::mOxyfontSmall, Recti(textX, y, 0, 10), "Scan the code, or go to", 1, blue);
		y += 14;
	}

	// The address, in pieces if it's long (without "https://")
	String address = mWebLinkUrl;
	if (address.startsWith("https://"))
		address.remove(0, 8);
	const int maxChars = std::max((panel.x + panel.width - 10 - textX) / 6, 8);
	for (int start = 0; start < address.length() && y < panel.y + panel.height - 30; start += maxChars)
	{
		drawer.printText(global::mOxyfontSmall, Recti(textX, y, 0, 10), address.getSubString(start, maxChars), 1, Color(1.0f, 0.85f, 0.3f, alpha));
		y += 12;
	}

	drawer.printText(global::mOxyfontTiny, Recti(panel.x + panel.width - 12, panel.y + panel.height - 16, 0, 10), "A / B: back", 3, Color(0.7f, 0.7f, 0.8f, alpha));
	drawer.performRendering();
}
#endif

void GameApp::onStartGame()
{
	mCurrentState = State::INGAME;
	mRemoveChild = mMenuBackground;
	mGameView->setFadedIn();
}

void GameApp::openTitleScreen()
{
	mRemoveChild = mMenuBackground;
	mGameView->setFadedIn();
	gotoPhase(1);
}

void GameApp::openMainMenu()
{
	Application::instance().getSimulation().setRunning(false);
	AudioOut::instance().stopSoundContext(AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_MUSIC);
	AudioOut::instance().stopSoundContext(AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_SOUND);

	if (mPauseMenu->getParent() == mGameView)
		mGameView->removeChild(*mPauseMenu);
	if (mTimeAttackResultsMenu->getParent() == mGameView)
		mGameView->removeChild(*mTimeAttackResultsMenu);

	mCurrentState = State::MAIN_MENU;
	mGameView->addChild(*mMenuBackground);
	mGameView->startFadingIn();

	mGameMenuManager->forceRemoveAll();
	mMenuBackground->openGameStartedMenu();

	Game::instance().resetCurrentMode();
}

void GameApp::openOptionsMenuInGame()
{
	mRestoreGameResolution = VideoOut::instance().getScreenRect().getSize();
	Application::instance().getSimulation().setSpeed(0.0f);

	mCurrentState = State::INGAME_OPTIONS;

	mPauseMenu->setEnabled(false);
	mGameView->addChild(*mMenuBackground);
	mGameView->startFadingIn();
	mMenuBackground->openOptions(true);
}

void GameApp::onFadedOutOptions()
{
	if (mCurrentState == State::INGAME_OPTIONS)
	{
		// Coming from in-game options, then go back into the game
		if (mMenuBackground->getParent() == mGameView)
			mGameView->removeChild(*mMenuBackground);

		mPauseMenu->setEnabled(true);
		mPauseMenu->onReturnFromOptions();

		ControlsIn::instance().setAllIgnores();

		VideoOut::instance().setScreenSize(mRestoreGameResolution);

		GameApp::instance().getGameView().startFadingIn(0.1f);

		// TODO: Fade out the context instead
		AudioOut::instance().stopSoundContext(AudioOut::CONTEXT_MENU + AudioOut::CONTEXT_MUSIC);

		Application::instance().getSimulation().setSpeed(1.0f);

		mCurrentState = State::INGAME;
	}
}

void GameApp::onGamePaused(bool canRestart)
{
	showSkippableCutsceneWindow(false);

	Application::instance().getSimulation().setSpeed(0.0f);
	AudioOut::instance().pauseSoundContext(AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_MUSIC);
	AudioOut::instance().pauseSoundContext(AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_SOUND);

	mPauseMenu->enableRestart(canRestart);
	mPauseMenu->onFadeIn();
	if (nullptr == mPauseMenu->getParent())
	{
		mGameView->addChild(*mPauseMenu);
	}
}

void GameApp::restartTimeAttack()
{
	mCurrentState = State::INGAME;
	Game::instance().restartTimeAttack(true);
	mGameView->removeChild(*mTimeAttackResultsMenu);
}

void GameApp::returnToMenu()
{
	openMainMenu();
}

void GameApp::showTimeAttackResults(int hundreds, const std::vector<int>& otherTimes)
{
	mCurrentState = State::TIME_ATTACK_RESULTS;
	if (mTimeAttackResultsMenu->getParent() != mGameView)
	{
		mTimeAttackResultsMenu->setYourTime(hundreds);
		for (int time : otherTimes)
		{
			mTimeAttackResultsMenu->addOtherTime(time);
		}
		mTimeAttackResultsMenu->onFadeIn();

		mGameView->addChild(*mTimeAttackResultsMenu);
	}
}

void GameApp::enableStillImageBlur(bool enable, float timeout)
{
	mGameView->setStillImageMode(enable ? GameView::StillImageMode::BLURRING : GameView::StillImageMode::NONE, timeout);
}

void GameApp::showUnlockedWindow(SecretUnlockedWindow::EntryType entryType, const std::string& title, const std::string& content)
{
	if (nullptr == mSecretUnlockedWindow)
	{
		mSecretUnlockedWindow = new SecretUnlockedWindow();
	}
	mGameView->addChild(*mSecretUnlockedWindow);
	mSecretUnlockedWindow->show(entryType, title, content, (entryType == SecretUnlockedWindow::EntryType::SECRET) ? 0x68 : 0x63);
#if defined(PLATFORM_3DS)
	n3ds::notificationlight::pulse();		// Soft purple glow of the notification light
#endif
}

void GameApp::showSkippableCutsceneWindow(bool show)
{
	if (nullptr == mSkippableCutsceneWindow)
	{
		if (!show)
			return;
		mSkippableCutsceneWindow = &mGameView->createChild<SkippableCutsceneWindow>();
	}
	else
	{
		if (nullptr == mSkippableCutsceneWindow->getParent())
			mGameView->addChild(*mSkippableCutsceneWindow);
	}
	mSkippableCutsceneWindow->show(show);
}

bool GameApp::supportsFileBrowser()
{
#if defined(SUPPORT_IMGUI)
	return true;
#else
	return false;
#endif
}

bool GameApp::openFileBrowser()
{
#if defined(SUPPORT_IMGUI)
	ImGuiManager::instance().getOrAddImGuiContentProvider<ImGuiFileBrowser>(100);
	return true;
#else
	return false;
#endif
}

#if defined(PLATFORM_3DS)
namespace
{
	// End of the 3DS bottom screen's save state files: this, then the size of the state data and its CRC32
	const char STATE_FOOTER_MAGIC[8] = { '3', 'D', 'S', 'C', 'R', 'C', '1', 0 };
	const size_t STATE_FOOTER_SIZE = 16;
}

void GameApp::updateBottomScreen()
{
	// 3DS bottom screen: controls overview with touch buttons for a save state per save file
	n3ds::filewriter::update();		// Files written in the background (save states, 3DS menu choices): report finished ones
	n3ds::screenshot::update();		// L + R
	Simulation& simulation = Application::instance().getSimulation();
	EmulatorInterface& emulatorInterface = EmulatorInterface::instance();

	// Play time and the day's stamp; saved only while a level is being played, not in menus (which may read from the SD card)
	const bool inLevel = (mCurrentState == State::INGAME) && (nullptr == mPauseMenu->getParent()) && simulation.isRunning() && emulatorInterface.readMemory8(0xfffff600) == 0x0c;
	n3ds::journal::update(inLevel);

	// Save file of the running game, from "global.active_saveslot": zero for "No Save", otherwise a pointer to the slot's data
	int slot = -1;
	{
		const uint32 slotPointer = emulatorInterface.readMemory32(0xffffe660);
		if (slotPointer == 0)
			slot = 0;
		else if (slotPointer >= 0xffffe6ac && (slotPointer - 0xffffe6ac) % 0x0a == 0 && (slotPointer - 0xffffe6ac) / 0x0a < 8)
			slot = 1 + (int)((slotPointer - 0xffffe6ac) / 0x0a);
	}
	const auto getStateFilename = [](int slot)
	{
		return Configuration::instance().mSaveStatesDirLocal + ((slot == 0) ? std::wstring(L"3ds_nosave.state") : (L"3ds_slot" + std::to_wstring(slot) + L".state"));
	};

	// Whether the current save file has a state. Checked on the SD card in the background when the save file changes: the SD card
	// can take seconds at times. "stateCheckGeneration" makes results of outdated checks get ignored.
	static int cachedSlot = -2;
	static bool cachedHasState = false;
	static int stateCheckGeneration = 0;

	// When a save file gets deleted in data select (its first byte gets the "empty" flag 0x80), delete its save state as well.
	// Not if all of them are empty at once: that's the default data before the save files are loaded (or nothing could be loaded).
	// And not for the save files that are empty when the save data first gets loaded: that only tried to delete save states that
	// don't exist (7 SD card accesses at startup, seen taking a second), keeping the SD card busy. Before the save data is loaded,
	// the memory is all zeros - which is also what save files in use look like. So the watching starts once some save file shows
	// as empty (the save data is there then), or after 5 seconds (all 8 save files in use, nothing would change at loading).
	{
		static uint8 lastFlags[8] = { 0 };
		static bool initialized = false;
		static const uint64 firstTicks = svcGetSystemTick();
		uint8 flags[8];
		int numEmpty = 0;
		for (int i = 0; i < 8; ++i)
		{
			flags[i] = emulatorInterface.readMemory8(0xffffe6ac + i * 0x0a);
			if (flags[i] & 0x80)
				++numEmpty;
		}
		if (!initialized && ((numEmpty > 0 && numEmpty < 8) || svcGetSystemTick() - firstTicks > 5 * (uint64)SYSCLOCK_ARM11))
		{
			initialized = true;		// From now on, a save file that becomes empty was deleted in data select
		}
		else if (initialized && numEmpty < 8)
		{
			for (int i = 0; i < 8; ++i)
			{
				const bool becameEmpty = (flags[i] & 0x80) && !(lastFlags[i] & 0x80);
				if (becameEmpty)
				{
					// In the background, in order after a write of it that may still be pending
					const int emptySlot = i + 1;
					const std::string path = rmx::convertToUTF8(getStateFilename(emptySlot));
					const std::shared_ptr<bool> removed = std::make_shared<bool>(false);
					n3ds::filewriter::runInBackground("deleting a save state",
						[path, removed]() { *removed = (unlink(path.c_str()) == 0); },
						[removed, emptySlot]()
						{
							if (*removed)
								RMX_LOG_INFO("Bottom screen: save file " << emptySlot << " is empty, deleted its save state");
						});
					if (emptySlot == cachedSlot)
					{
						cachedHasState = false;
						++stateCheckGeneration;
					}
				}
			}
		}
		memcpy(lastFlags, flags, 8);
	}

	if (slot != cachedSlot)
	{
		cachedSlot = slot;
		cachedHasState = false;
		const int generation = ++stateCheckGeneration;
		if (slot >= 0)
		{
			const std::string path = rmx::convertToUTF8(getStateFilename(slot));
			const std::shared_ptr<bool> exists = std::make_shared<bool>(false);
			n3ds::filewriter::runInBackground("checking for a save state",
				[path, exists]() { struct stat info; *exists = (stat(path.c_str(), &info) == 0); },
				[exists, generation]()
				{
					if (generation == stateCheckGeneration)
						cachedHasState = *exists;
				});
		}
	}

	// Save states only in levels and special stages of a normal game (game mode 0x0c / 0x34), not paused
	const uint8 gameMode = emulatorInterface.readMemory8(0xfffff600);
	const Game::Mode mode = Game::instance().getMode();
	const bool saveStatesAllowed = (mCurrentState == State::INGAME) && (nullptr == mPauseMenu->getParent()) && simulation.isRunning()
								&& (gameMode == 0x0c || gameMode == 0x34) && slot >= 0
								&& (mode == Game::Mode::NORMAL_GAME || mode == Game::Mode::ACT_SELECT || mode == Game::Mode::UNDEFINED);	// Undefined after loading a state

	const n3ds::bottomscreen::Request request = n3ds::bottomscreen::update(saveStatesAllowed, slot, cachedHasState);
	if (request == n3ds::bottomscreen::Request::NONE)
		return;

	const std::wstring filename = getStateFilename(slot);
	const uint64 startTicks = svcGetSystemTick();
	if (request == n3ds::bottomscreen::Request::SAVE_STATE)
	{
		// Directly with the serializer instead of "Simulation::saveState", which also saves a screenshot and shows an error dialog on failure
		SaveStateSerializer serializer(simulation, RenderParts::instance());
		std::vector<uint8> data;
		if (!serializer.saveState(data))
		{
			RMX_LOG_INFO("Bottom screen: save state for save file " << slot << " FAILED");
			n3ds::bottomscreen::showResult(n3ds::bottomscreen::Result::FAILED);
			return;
		}

		// A checksum at the end, so a damaged file is noticed before loading it into the game (see below)
		{
			const uint32 size = (uint32)data.size();
			const uint32 checksum = rmx::getCRC32(data.data(), data.size());
			const size_t pos = data.size();
			data.resize(pos + STATE_FOOTER_SIZE);
			memcpy(&data[pos], STATE_FOOTER_MAGIC, 8);
			memcpy(&data[pos + 8], &size, 4);
			memcpy(&data[pos + 12], &checksum, 4);
		}

		// The file gets written in the background, as the SD card sometimes takes seconds to respond (the game froze for that long).
		// Loading waits for it if needed, so the state counts as there right away.
		const uint32 serializeMs = (uint32)((svcGetSystemTick() - startTicks) * 1000 / SYSCLOCK_ARM11);
		const int sizeKB = (int)(data.size() / 1024);
		cachedHasState = true;
		++stateCheckGeneration;		// A check still running would not know about this one yet
		n3ds::filewriter::writeFile(rmx::convertToUTF8(filename), std::move(data), [slot, sizeKB, serializeMs](bool success, uint32 writeMs)
		{
			RMX_LOG_INFO("Bottom screen: save state for save file " << slot << (success ? " saved" : " FAILED") << ", " << sizeKB << " KB, "
						 << serializeMs << " ms + " << writeMs << " ms writing in the background");
			n3ds::bottomscreen::showResult(success ? n3ds::bottomscreen::Result::SAVED : n3ds::bottomscreen::Result::FAILED);
		});
	}
	else if (request == n3ds::bottomscreen::Request::LOAD_STATE)
	{
		n3ds::filewriter::waitUntilDone();		// In case the state was just saved and is still getting written
		if (!FTX::FileSystem->exists(filename))
		{
			n3ds::bottomscreen::showResult(n3ds::bottomscreen::Result::NO_STATE);
			return;
		}
		// Save states from this version on end with a checksum: a damaged one doesn't get loaded at all. Older ones have none.
		{
			std::vector<uint8> content;
			if (!FTX::FileSystem->readFile(filename, content))
			{
				n3ds::bottomscreen::showResult(n3ds::bottomscreen::Result::FAILED);
				return;
			}
			const size_t footer = content.size() - std::min(content.size(), STATE_FOOTER_SIZE);
			if (content.size() >= STATE_FOOTER_SIZE && memcmp(&content[footer], STATE_FOOTER_MAGIC, 8) == 0)
			{
				uint32 size = 0;
				uint32 checksum = 0;
				memcpy(&size, &content[footer + 8], 4);
				memcpy(&checksum, &content[footer + 12], 4);
				if (size != footer || checksum != rmx::getCRC32(content.data(), footer))
				{
					RMX_LOG_INFO("Bottom screen: save state for save file " << slot << " is damaged (checksum), not loaded");
					n3ds::bottomscreen::showResult(n3ds::bottomscreen::Result::FAILED);
					return;
				}
			}
		}

		// A save state contains the game's whole memory, including its in-memory copy of the save data (all data select
		// slots, competition records, Blue Sphere progress). Keep the current copy instead of the older one from the state,
		// otherwise the next time the game saves, it would write the old data back and roll back progress in other slots.
		struct PersistentRange { uint32 mAddress; uint32 mSize; };
		static const PersistentRange PERSISTENT_RANGES[] =
		{
			{ 0xffffe600, 0x50 },	// Competition mode records
			{ 0xffffe6ac, 0x50 },	// Data select save slots
			{ 0x801100, 0x100 },	// Data select save slots, extended data
			{ 0xffffffa6, 4 },		// Blue Sphere progress
		};
		std::vector<uint8> persistentBackup;
		for (const PersistentRange& range : PERSISTENT_RANGES)
		{
			for (uint32 k = 0; k < range.mSize; ++k)
				persistentBackup.push_back(emulatorInterface.readMemory8(range.mAddress + k));
		}

		// The current state, to go back to if loading fails: the state gets read piece by piece into the game's memory and scripts,
		// so a damaged file or one the scripts don't fit any more (from another version of the game) could fail halfway, leaving a
		// mix of both behind. With the persistent data from that, the next save could even damage the save files.
		std::vector<uint8> currentState;
		const bool hasCurrentState = SaveStateSerializer(simulation, RenderParts::instance()).saveState(currentState);

		// Same as the engine's save state menu, except the music: if the same one fits after loading (like in Blue Sphere,
		// or the same level), it continues instead of starting over
		AudioOut::instance().keepMusicOnNextReset();
		EngineMain::getDelegate().onPreSaveStateLoad();
		const bool success = simulation.loadState(filename, false);
		bool restored = false;
		if (!success && hasCurrentState)
		{
			restored = SaveStateSerializer(simulation, RenderParts::instance()).loadState(currentState);
			if (restored)
				simulation.getCodeExec().reinitRuntime(nullptr, CodeExec::CallStackInitPolicy::USE_EXISTING);
			RMX_LOG_INFO("Bottom screen: loading the save state failed, " << (restored ? "the game continues where it was" : "and going back FAILED too"));
		}
		if (success || restored)
		{
			size_t index = 0;
			for (const PersistentRange& range : PERSISTENT_RANGES)
			{
				for (uint32 k = 0; k < range.mSize; ++k)
					emulatorInterface.writeMemory8(range.mAddress + k, persistentBackup[index++]);
			}

			// Loading a state stops all sounds, and the game itself picks the music again only on certain events (like the
			// next act or the end of a power-up), so it would stay silent until then. Start the fitting music right away,
			// with the game's own logic: level music, or boss, Super or invincibility music. Blue Sphere in special stages.
			LemonScriptRuntime& scriptRuntime = simulation.getCodeExec().getLemonScriptRuntime();
			if (emulatorInterface.readMemory8(0xfffff600) == 0x34)
			{
				lemon::Runtime::FunctionCallParameters params;
				params.mParams.emplace_back(lemon::PredefinedDataTypes::UINT_8, 0x1c);
				scriptRuntime.getInternalLemonRuntime().callFunctionWithParameters("playMusic", params);
			}
			else
			{
				scriptRuntime.callFunctionByName("chooseFittingMusic", false);
			}
		}
		AudioOut::instance().finishKeepingMusicSoon();
		RMX_LOG_INFO("Bottom screen: save state for save file " << slot << (success ? " loaded" : " FAILED to load") << " (" << (int)((svcGetSystemTick() - startTicks) * 1000 / SYSCLOCK_ARM11) << " ms)");
		n3ds::bottomscreen::showResult(success ? n3ds::bottomscreen::Result::LOADED : n3ds::bottomscreen::Result::FAILED);
	}
}
#endif

void GameApp::gotoPhase(int phaseNumber)
{
	switch (phaseNumber)
	{
		case 0:
		{
			// Start with the disclaimer
			mCurrentState = State::DISCLAIMER;
			mStateTimeout = 8.0f;
			InputManager::instance().setTouchInputMode(InputManager::TouchInputMode::FULLSCREEN_START);

			// Load disclaimer texture if not done already
			if (!mDisclaimerTexture.isValid())
			{
				FileHelper::loadTexture(mDisclaimerTexture, L"data/images/menu/disclaimer.png");
			}
			break;
		}

		case 1:
		{
			// Start with the intro & title screen
			mCurrentState = State::TITLE_SCREEN;
			Game::instance().startIntoTitleScreen();
			break;
		}

		case 2:
		{
			// Start with the main menu
			openMainMenu();
			break;
		}

		case 3:
		{
			// Start in-game
			mCurrentState = State::INGAME;
			Game::instance().resetCurrentMode();
			Simulation& simulation = Application::instance().getSimulation();
			simulation.setRunning(true);
			break;
		}
	}
}
