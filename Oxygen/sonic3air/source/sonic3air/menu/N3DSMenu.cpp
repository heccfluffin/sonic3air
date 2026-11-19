/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the "3DS" menu, where Play Coins can be spent on bottom screen themes, secrets and a starting shield.
*	Built like the Extras menu, so it looks and behaves like the other A.I.R. menus.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"

#if defined(PLATFORM_3DS)

#include "sonic3air/menu/N3DSMenu.h"
#include "sonic3air/menu/MenuBackground.h"
#include "sonic3air/menu/SharedResources.h"
#include "sonic3air/Game.h"
#include "sonic3air/audio/AudioOut.h"
#include "sonic3air/data/PlayerProgress.h"
#include "sonic3air/data/SharedDatabase.h"

#include "oxygen/application/EngineMain.h"
#include "oxygen/application/input/InputManager.h"
#include "oxygen/helper/Utils.h"
#include "oxygen/platform/3ds/N3DSBottomScreen.h"
#include "oxygen/platform/3ds/N3DSPlayCoins.h"
#include "oxygen/platform/3ds/N3DSShop.h"


namespace
{
	static constexpr uint32 BACK = 0xffff;
	static constexpr uint32 SHIELD_BUY = 0x100;
	static constexpr uint32 SHIELD_SELECT = 0x200;		// Plus the shield, see n3ds::shop::Shield

	struct ThemeInfo
	{
		const char* mName;
		const char* mDescription;
		int mPrice;
		Color mLight;		// Colors for the little preview in the menu
		Color mDark;
		Color mSave;
		Color mLoad;
	};

	Color rgb(int r, int g, int b)  { return Color((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f); }

	// Same order and colors as THEMES in build/_3ds/bottomscreen/make_bottom_screen.py
	const ThemeInfo THEMES[n3ds::shop::_NUM_THEMES] =
	{
		{ "Blue Zig-Zag",	 "The classic look of the bottom screen.",					0,  rgb(34, 104, 214), rgb(12, 38, 122), rgb(50, 175, 70),   rgb(30, 165, 235) },
		{ "Sky Aero",		 "Glossy sky, light rays and a green hill.",				30, rgb(140, 230, 250), rgb(20, 90, 200), rgb(110, 200, 40), rgb(30, 105, 215) },
		{ "Crimson Zig-Zag", "Bold red stripes with fiery buttons.",					30, rgb(205, 45, 55),  rgb(95, 12, 28),  rgb(230, 110, 20), rgb(205, 45, 45) },
		{ "Golden Zig-Zag",	 "Shiny gold stripes, fit for a super form.",				40, rgb(240, 170, 30), rgb(160, 80, 10), rgb(235, 175, 25), rgb(150, 160, 180) },
		{ "Emerald Zig-Zag", "Green stripes, as precious as the Master Emerald.",		30, rgb(40, 175, 95),  rgb(10, 85, 45),  rgb(190, 205, 30), rgb(20, 150, 150) },
		{ "Starry Night",	 "Midnight blue with twinkling stars.",					40, rgb(30, 45, 100),  rgb(8, 12, 40),   rgb(175, 190, 215), rgb(85, 70, 190) },
	};

	struct SecretInfo
	{
		uint32 mSecret;
		int mPrice;
	};

	// Secrets that are normally unlocked by completing achievements (in that order)
	const SecretInfo SECRETS[] =
	{
		{ SharedDatabase::Secret::SECRET_DROPDASH,		 60 },
		{ SharedDatabase::Secret::SECRET_KNUX_AND_TAILS, 80 },
		{ SharedDatabase::Secret::SECRET_SUPER_PEELOUT,	 90 },
		{ SharedDatabase::Secret::SECRET_DEBUGMODE,		 110 },
		{ SharedDatabase::Secret::SECRET_BLUE_SPHERE,	 120 },
	};

	const constexpr int SHIELD_PRICE = 50;

	struct ShieldInfo
	{
		const char* mName;
		const char* mDescription;
		Color mColor;
	};
	const ShieldInfo SHIELDS[n3ds::shop::_NUM_SHIELDS] =
	{
		{ "Off",			  "No starting shield.",														  rgb(90, 90, 100) },
		{ "Classic Shield",	  "The blue shield. Needs \"Shield Types\" to include the classic shield.",	  rgb(70, 140, 255) },
		{ "Fire Shield",	  "Burn through enemies with a fiery dash.",									  rgb(255, 120, 30) },
		{ "Lightning Shield", "Double jump and attract nearby rings.",									  rgb(255, 230, 60) },
		{ "Bubble Shield",	  "Breathe underwater and bounce off the ground.",							  rgb(90, 200, 255) },
	};

	int getSecretPrice(uint32 secret)
	{
		for (const SecretInfo& info : SECRETS)
		{
			if (info.mSecret == secret)
				return info.mPrice;
		}
		return 0;
	}
}


N3DSMenu::N3DSMenu(MenuBackground& menuBackground) :
	mMenuBackground(&menuBackground)
{
	mScrolling.setVisibleAreaHeight(224 - 30 - 16);		// Without the tab title and the Play Coins bar at the bottom

	mTabMenuEntries.addEntry("", 0)
					.addOption("THEMES",  Tab::Id::THEMES)
					.addOption("SECRETS", Tab::Id::SECRETS)
					.addOption("SHIELD",  Tab::Id::SHIELD);

	for (int i = 0; i < 3; ++i)
	{
		GameMenuEntries& entries = mTabs[i].mMenuEntries;
		entries.reserve(10);
		entries.addEntry();		// Dummy entry representing the title in menu navigation
	}
}

N3DSMenu::~N3DSMenu()
{
}

GameMenuBase::BaseState N3DSMenu::getBaseState() const
{
	switch (mState)
	{
		case State::APPEAR:		  return BaseState::FADE_IN;
		case State::SHOW:		  return BaseState::SHOW;
		case State::CONFIRM:	  return BaseState::SHOW;
		case State::FADE_TO_MENU: return BaseState::FADE_OUT;
		default:				  return BaseState::INACTIVE;
	}
}

void N3DSMenu::setBaseState(BaseState baseState)
{
	switch (baseState)
	{
		case BaseState::INACTIVE: mState = State::INACTIVE;  break;
		case BaseState::FADE_IN:  mState = State::APPEAR;  break;
		case BaseState::SHOW:	  mState = State::SHOW;  break;
		case BaseState::FADE_OUT: mState = State::FADE_TO_MENU;  break;
	}
}

void N3DSMenu::onFadeIn()
{
	mState = State::APPEAR;

	// Read the Play Coins each time the menu opens (they may have changed while the game was suspended)
	int coins = 0;
	mPlayCoins = n3ds::playcoins::read(coins) ? coins : -1;
	mMessage.clear();
	mPreviewedTheme = -1;

	rebuildTabs();

	mMenuBackground->showPreview(false, false);
	mMenuBackground->startTransition(MenuBackground::Target::BLUE);

	for (size_t k = 0; k < 3; ++k)
	{
		for (size_t i = 0; i < mTabs[k].mMenuEntries.size(); ++i)
		{
			mTabs[k].mMenuEntries[i].mAnimation.mHighlight = 0.0f;
		}
	}

	AudioOut::instance().setMenuMusic(0x2f);
}

bool N3DSMenu::canBeRemoved()
{
	return (mState == State::INACTIVE && mVisibility <= 0.0f);
}

void N3DSMenu::initialize()
{
}

void N3DSMenu::deinitialize()
{
}

void N3DSMenu::keyboard(const rmx::KeyboardEvent& ev)
{
}

void N3DSMenu::rebuildTabs()
{
	const n3ds::shop::Data& shop = n3ds::shop::get();

	// Themes
	{
		GameMenuEntries& entries = mTabs[Tab::Id::THEMES].mMenuEntries;
		const int selected = entries.mSelectedEntryIndex;
		entries.resize(1);
		for (int theme = 0; theme < n3ds::shop::_NUM_THEMES; ++theme)
			entries.addEntry(THEMES[theme].mName, theme);
		entries.addEntry("Back", BACK);
		entries.mSelectedEntryIndex = clamp(selected, 0, (int)entries.size() - 1);
	}

	// Secrets
	{
		GameMenuEntries& entries = mTabs[Tab::Id::SECRETS].mMenuEntries;
		const int selected = entries.mSelectedEntryIndex;
		entries.resize(1);
		for (const SecretInfo& info : SECRETS)
		{
			const SharedDatabase::Secret* secret = SharedDatabase::getSecret(info.mSecret);
			if (nullptr != secret)
				entries.addEntry(secret->mName, info.mSecret);
		}
		entries.addEntry("Back", BACK);
		entries.mSelectedEntryIndex = clamp(selected, 0, (int)entries.size() - 1);
	}

	// Starting shield
	{
		GameMenuEntries& entries = mTabs[Tab::Id::SHIELD].mMenuEntries;
		const int selected = entries.mSelectedEntryIndex;
		entries.resize(1);
		if (shop.mOwnsShield)
		{
			for (int shield = 0; shield < n3ds::shop::_NUM_SHIELDS; ++shield)
				entries.addEntry(SHIELDS[shield].mName, SHIELD_SELECT + shield);
		}
		else
		{
			entries.addEntry("Starting Shield", SHIELD_BUY);
		}
		entries.addEntry("Back", BACK);
		entries.mSelectedEntryIndex = clamp(selected, 0, (int)entries.size() - 1);
	}
}

bool N3DSMenu::getItem(size_t tabIndex, uint32 data, Item& outItem) const
{
	const n3ds::shop::Data& shop = n3ds::shop::get();
	switch (tabIndex)
	{
		case Tab::Id::THEMES:
			if (data >= (uint32)n3ds::shop::_NUM_THEMES)
				return false;
			outItem.mName = THEMES[data].mName;
			outItem.mPrice = THEMES[data].mPrice;
			outItem.mOwned = (shop.mOwnedThemes & (1 << data)) != 0;
			return true;

		case Tab::Id::SECRETS:
		{
			const SharedDatabase::Secret* secret = SharedDatabase::getSecret(data);
			if (nullptr == secret)
				return false;
			outItem.mName = secret->mName;
			outItem.mPrice = getSecretPrice(data);
			outItem.mOwned = PlayerProgress::instance().mUnlocks.isSecretUnlocked(data);
			return true;
		}

		case Tab::Id::SHIELD:
			if (data != SHIELD_BUY)
				return false;
			outItem.mName = "Starting Shield";
			outItem.mPrice = SHIELD_PRICE;
			outItem.mOwned = shop.mOwnsShield;
			return true;
	}
	return false;
}

void N3DSMenu::showMessage(const std::string& text, bool isError)
{
	mMessage = text;
	mMessageIsError = isError;
	mMessageTimeout = 3.0f;
}

void N3DSMenu::updateThemePreview()
{
	// While a theme is selected in the menu, show it on the bottom screen; otherwise the active one
	int theme = n3ds::shop::get().mSelectedTheme;
	if (mState != State::FADE_TO_MENU && mState != State::INACTIVE && mActiveTab == Tab::Id::THEMES && mActiveMenu == &mTabs[Tab::Id::THEMES].mMenuEntries)
	{
		const uint32 data = mActiveMenu->selected().mData;
		if (data < (uint32)n3ds::shop::_NUM_THEMES)
			theme = (int)data;
	}
	if (theme != mPreviewedTheme)
	{
		mPreviewedTheme = theme;
		n3ds::bottomscreen::setTheme(theme);
	}
}

void N3DSMenu::update(float timeElapsed)
{
	GameMenuBase::update(timeElapsed);

	mActiveTabAnimated += clamp((float)mActiveTab - mActiveTabAnimated, -timeElapsed * 4.0f, timeElapsed * 4.0f);
	if (mMessageTimeout > 0.0f)
		mMessageTimeout -= timeElapsed;

	const InputManager::ControllerScheme& keys = InputManager::instance().getController(0);
	const bool accept = (keys.Start.justPressed() || keys.A.justPressed() || keys.X.justPressed());
	const bool back = (keys.Back.justPressed() || keys.B.justPressed());

	if (mState == State::CONFIRM)
	{
		if (accept)
		{
			buySelected();
		}
		else if (back)
		{
			playMenuSound(0xad);
			mState = State::SHOW;
		}
	}
	else if (mState == State::SHOW)
	{
		if (mActiveMenu == &mTabMenuEntries && (keys.Down.justPressedOrRepeat() || keys.Up.justPressedOrRepeat()))
		{
			// Switch from title to tab content
			mActiveMenu = &mTabs[mActiveTab].mMenuEntries;
			mActiveMenu->mSelectedEntryIndex = 0;
		}

		const GameMenuEntries::UpdateResult result = mActiveMenu->update();
		if (result != GameMenuEntries::UpdateResult::NONE)
		{
			if (result == GameMenuEntries::UpdateResult::OPTION_CHANGED && mActiveMenu == &mTabMenuEntries)
			{
				mActiveTab = mTabMenuEntries[0].mSelectedIndex;
				playMenuSound(0xb7);
			}
			else
			{
				playMenuSound(0x5b);
				if (result == GameMenuEntries::UpdateResult::ENTRY_CHANGED && mActiveMenu != &mTabMenuEntries && mActiveMenu->mSelectedEntryIndex == 0)
				{
					// Switch from tab content to title
					mActiveMenu = &mTabMenuEntries;
				}
			}
		}

		if (back)
		{
			goBack();
		}
		else if (accept)
		{
			if (mActiveMenu == &mTabMenuEntries || mActiveMenu->selected().mData == BACK)
				goBack();
			else
				onAccept();
		}
	}

	updateThemePreview();

	// Update animation
	for (size_t k = 0; k < 3; ++k)
	{
		const float maxStep = timeElapsed * 10.0f;
		for (size_t i = 0; i < mTabs[k].mMenuEntries.size(); ++i)
		{
			const float target = ((int)i == mTabs[k].mMenuEntries.mSelectedEntryIndex && k == mActiveTab && mActiveMenu != &mTabMenuEntries) ? 1.0f : 0.0f;
			mTabs[k].mMenuEntries[i].mAnimation.mHighlight += clamp(target - mTabs[k].mMenuEntries[i].mAnimation.mHighlight, -maxStep, maxStep);
		}
	}

	mScrolling.update(timeElapsed);

	// Fading in/out
	if (mState == State::APPEAR)
	{
		if (updateFadeIn(timeElapsed * 6.0f))
		{
			mState = State::SHOW;
		}
	}
	else if (mState == State::FADE_TO_MENU)
	{
		if (updateFadeOut(timeElapsed * 4.0f))
		{
			mState = State::INACTIVE;
		}
	}
}

void N3DSMenu::onAccept()
{
	n3ds::shop::Data& shop = n3ds::shop::get();
	const uint32 data = mActiveMenu->selected().mData;

	// Owned themes and shields: select them
	if (mActiveTab == Tab::Id::THEMES && data < (uint32)n3ds::shop::_NUM_THEMES && (shop.mOwnedThemes & (1 << data)) != 0)
	{
		if (shop.mSelectedTheme != (int)data)
		{
			shop.mSelectedTheme = (int)data;
			n3ds::shop::save();
			playMenuSound(0x63);
			showMessage(std::string(THEMES[data].mName) + " is now active", false);
		}
		return;
	}
	if (mActiveTab == Tab::Id::SHIELD && data >= SHIELD_SELECT && data < SHIELD_SELECT + n3ds::shop::_NUM_SHIELDS)
	{
		const int shield = (int)(data - SHIELD_SELECT);
		if (shop.mSelectedShield != shield)
		{
			shop.mSelectedShield = shield;
			n3ds::shop::save();
			playMenuSound(0x63);
			showMessage((shield == n3ds::shop::SHIELD_OFF) ? "Starting shield switched off" : (std::string("Starting with the ") + SHIELDS[shield].mName), false);
		}
		return;
	}

	// Everything else can be bought, if not owned already
	Item item;
	if (!getItem(mActiveTab, data, item) || item.mOwned)
	{
		playMenuSound(0xad);
		return;
	}
	if (mPlayCoins < 0)
	{
		playMenuSound(0xad);
		showMessage("Play Coins could not be read", true);
		return;
	}
	if (mPlayCoins < item.mPrice)
	{
		playMenuSound(0xad);
		showMessage("Not enough Play Coins - walk with your 3DS to earn more", true);
		return;
	}

	playMenuSound(0x63);
	mState = State::CONFIRM;
}

void N3DSMenu::buySelected()
{
	mState = State::SHOW;

	const uint32 data = mActiveMenu->selected().mData;
	Item item;
	if (!getItem(mActiveTab, data, item) || item.mOwned)
		return;

	int coinsLeft = mPlayCoins;
	if (!n3ds::playcoins::spend(item.mPrice, coinsLeft))
	{
		mPlayCoins = coinsLeft;
		playMenuSound(0xad);
		showMessage("Could not spend the Play Coins, nothing was bought", true);
		return;
	}
	mPlayCoins = coinsLeft;

	n3ds::shop::Data& shop = n3ds::shop::get();
	switch (mActiveTab)
	{
		case Tab::Id::THEMES:
			shop.mOwnedThemes |= (1 << data);
			shop.mSelectedTheme = (int)data;		// Use it right away
			n3ds::shop::save();
			break;

		case Tab::Id::SECRETS:
			PlayerProgress::instance().mUnlocks.setSecretUnlocked(data);
			PlayerProgress::instance().save();
			break;

		case Tab::Id::SHIELD:
			shop.mOwnsShield = true;
			shop.mSelectedShield = n3ds::shop::SHIELD_CLASSIC;
			n3ds::shop::save();
			break;
	}
	RMX_LOG_INFO("3DS menu: bought '" << item.mName << "' for " << item.mPrice << " Play Coins");

	playMenuSound(0x33);	// Ring sound
	showMessage(item.mName + " bought!", false);
	rebuildTabs();
}

void N3DSMenu::render()
{
	GuiBase::render();

	Drawer& drawer = EngineMain::instance().getDrawer();
	const n3ds::shop::Data& shop = n3ds::shop::get();

	int anchorX = 200;
	float alpha = 1.0f;
	if (mState != State::SHOW && mState != State::CONFIRM)
	{
		anchorX += roundToInt((1.0f - mVisibility) * 300.0f);
		alpha = mVisibility;
	}

	const int startY = 30 - mScrolling.getScrollOffsetYInt();
	drawer.pushScissor(Recti(0, 30, (int)mRect.width, (int)mRect.height - 30 - 16));

	const int minTabIndex = (int)std::floor(mActiveTabAnimated);
	const int maxTabIndex = (int)std::ceil(mActiveTabAnimated);

	for (int tabIndex = minTabIndex; tabIndex <= maxTabIndex; ++tabIndex)
	{
		const Tab& tab = mTabs[tabIndex];
		const float tabAlpha = alpha * (1.0f - std::fabs(tabIndex - mActiveTabAnimated));
		const int baseX = anchorX + roundToInt((tabIndex - mActiveTabAnimated) * 250);
		int py = startY + 12;

		// Short explanation at the top of each tab
		{
			const char* text = (tabIndex == Tab::Id::THEMES) ? "Pick a look for the bottom screen" :
							   (tabIndex == Tab::Id::SECRETS) ? "Unlock secrets early with Play Coins" :
							   shop.mOwnsShield ? "Start each act and life with this shield" : "Start each act and life with a shield";
			py += 3;
			drawer.printText(global::mSonicFontB, Recti(baseX, py, 0, 10), text, 5, Color(0.6f, 0.8f, 1.0f, tabAlpha));
			py += 10;
		}

		for (size_t line = 1; line < tab.mMenuEntries.size(); ++line)
		{
			const auto& entry = tab.mMenuEntries[line];
			const bool isSelected = (mActiveMenu == &tab.mMenuEntries && (int)line == tab.mMenuEntries.mSelectedEntryIndex);
			const Color color = isSelected ? Color(1.0f, 1.0f, 0.0f, tabAlpha) : Color(1.0f, 1.0f, 1.0f, tabAlpha);
			const Color color2 = isSelected ? Color(1.0f, 1.0f, 0.6f, tabAlpha) : Color(0.9f, 0.9f, 0.9f, tabAlpha);
			int currentAbsoluteY1 = py - startY;

			if (entry.mData == BACK)
			{
				py += 22;
				drawer.printText(global::mOxyfontRegular, Recti(baseX, py, 0, 10), entry.mText, 5, color);
				py += 16;
			}
			else
			{
				py += 8;
				const int localStartY = py;
				const int px = baseX + roundToInt(interpolate(12.0f, 0.0f, entry.mAnimation.mHighlight));

				// Card background, like in the Extras menu
				DrawerTexture& frameTexture = global::mAchievementsFrame;
				drawer.drawRect(Recti(px - 185, py, frameTexture.getWidth(), frameTexture.getHeight()), frameTexture, Color(1.0f, 1.0f, 1.0f, tabAlpha));

				// Picture on the left, and texts
				const Recti imageRect(px - 183, py + 2, 64, 48);
				std::string title = entry.mText;
				std::string description;
				std::string status;
				Color statusColor(1.0f, 1.0f, 1.0f, tabAlpha);

				Item item;
				const bool isItem = getItem(tabIndex, entry.mData, item);
				if (isItem)
				{
					if (item.mOwned)
					{
						status = (tabIndex == Tab::Id::SECRETS) ? "UNLOCKED" : "OWNED";
						statusColor = Color(0.5f, 1.0f, 0.5f, tabAlpha);
					}
					else
					{
						status = String(0, "%d PLAY COINS", item.mPrice).toStdString();
						statusColor = (mPlayCoins >= item.mPrice) ? Color(1.0f, 0.85f, 0.2f, tabAlpha) : Color(0.7f, 0.5f, 0.5f, tabAlpha);
					}
				}

				if (tabIndex == Tab::Id::THEMES)
				{
					const ThemeInfo& theme = THEMES[entry.mData];
					description = theme.mDescription;
					if (shop.mSelectedTheme == (int)entry.mData)
					{
						status = "ACTIVE";
						statusColor = Color(0.4f, 1.0f, 1.0f, tabAlpha);
					}
					else if (theme.mPrice == 0)
					{
						status = "FREE";
					}

					// Little preview: stripes in the theme's colors, with its save / load button colors
					drawer.drawRect(imageRect, Color(theme.mDark.r, theme.mDark.g, theme.mDark.b, tabAlpha));
					for (int k = 0; k < 3; ++k)
						drawer.drawRect(Recti(imageRect.x, imageRect.y + 4 + k * 16, imageRect.width, 8), Color(theme.mLight.r, theme.mLight.g, theme.mLight.b, tabAlpha));
					drawer.drawRect(Recti(imageRect.x + 34, imageRect.y + 6, 26, 16), Color(theme.mSave.r, theme.mSave.g, theme.mSave.b, tabAlpha));
					drawer.drawRect(Recti(imageRect.x + 34, imageRect.y + 26, 26, 16), Color(theme.mLoad.r, theme.mLoad.g, theme.mLoad.b, tabAlpha));
				}
				else if (tabIndex == Tab::Id::SECRETS)
				{
					const SharedDatabase::Secret* secret = SharedDatabase::getSecret(entry.mData);
					if (nullptr != secret)
					{
						description = secret->mDescription;
						DrawerTexture& image = item.mOwned ? global::mSecretImage[secret->mType] : global::mSecretImage[secret->mType | 0x80000000];
						if (image.getWidth() > 0)
							drawer.drawRect(Recti(imageRect.x, imageRect.y, image.getWidth(), image.getHeight()), image, Color(1.0f, 1.0f, 1.0f, tabAlpha));
						if (!item.mOwned)
							description += String(0, " (Or complete %d achievements.)", secret->mRequiredAchievements).toStdString();
					}
				}
				else if (tabIndex == Tab::Id::SHIELD)
				{
					const int shield = (entry.mData == SHIELD_BUY) ? (int)n3ds::shop::SHIELD_CLASSIC : (int)(entry.mData - SHIELD_SELECT);
					const Color& shieldColor = SHIELDS[clamp(shield, 0, (int)n3ds::shop::_NUM_SHIELDS - 1)].mColor;
					if (entry.mData == SHIELD_BUY)
					{
						description = "Buy once, then choose Classic, Fire, Lightning or Bubble any time.";
					}
					else
					{
						description = SHIELDS[shield].mDescription;
						if (shop.mSelectedShield == shield)
						{
							status = "ACTIVE";
							statusColor = Color(0.4f, 1.0f, 1.0f, tabAlpha);
						}
					}

					// A round shield in its color
					drawer.drawRect(imageRect, Color(0.05f, 0.08f, 0.2f, tabAlpha));
					for (int k = 0; k < 18; ++k)
					{
						const int halfWidth = roundToInt(std::sqrt((float)(18 * 18 - (k - 9) * (k - 9) * 4)) * 0.9f);
						drawer.drawRect(Recti(imageRect.x + 32 - halfWidth, imageRect.y + 6 + k * 2, halfWidth * 2, 2), Color(shieldColor.r, shieldColor.g, shieldColor.b, tabAlpha * 0.85f));
					}
				}

				py += 7;
				String name(title);
				name.upperCase();
				drawer.printText(global::mOxyfontRegular, Recti(px - 110, py, 0, 10), name, 1, color);
				if (!status.empty())
					drawer.printText(global::mOxyfontSmall, Recti(px + 170, py + 1, 0, 10), status, 3, statusColor);

				// Description, wrapped
				{
					std::vector<std::string_view> textLines;
					utils::splitTextIntoLines(textLines, description, global::mOxyfontTiny, 260);
					py += (textLines.size() == 1) ? 9 : 5;
					for (size_t k = 0; k < textLines.size() && k < 3; ++k)
					{
						py += 10;
						drawer.printText(global::mOxyfontTiny, Recti(px - 105, py, 0, 10), textLines[k], 1, color2);
					}
				}
				py = localStartY + 56;
			}

			if (isSelected)
			{
				const int currentAbsoluteY2 = py - startY;
				mScrolling.setCurrentSelection(currentAbsoluteY1 - 28, currentAbsoluteY2 + 34);
			}
		}
	}

	drawer.popScissor();

	// Play Coins bar at the bottom
	{
		const int barY = (int)mRect.height - 16;
		drawer.drawRect(Recti(anchorX - 200, barY, 400, 16), Color(0.0f, 0.05f, 0.2f, alpha * 0.85f));
		const std::string coinsText = (mPlayCoins >= 0) ? String(0, "PLAY COINS: %d", mPlayCoins).toStdString() : std::string("PLAY COINS: ---");
		drawer.printText(global::mOxyfontSmall, Recti(anchorX - 192, barY + 3, 0, 10), coinsText, 1, Color(1.0f, 0.85f, 0.2f, alpha));

		if (mMessageTimeout > 0.0f && !mMessage.empty())
		{
			const float messageAlpha = alpha * std::min(mMessageTimeout * 2.0f, 1.0f);
			const Color messageColor = mMessageIsError ? Color(1.0f, 0.5f, 0.4f, messageAlpha) : Color(0.6f, 1.0f, 0.6f, messageAlpha);
			drawer.printText(global::mOxyfontSmall, Recti(anchorX + 192, barY + 3, 0, 10), mMessage, 3, messageColor);
		}
		else
		{
			drawer.printText(global::mOxyfontTiny, Recti(anchorX + 192, barY + 4, 0, 10), "Earn up to 10 a day by walking with your 3DS", 3, Color(0.8f, 0.85f, 1.0f, alpha * 0.8f));
		}
	}

	// Tab titles, like in the Extras menu
	{
		drawer.drawRect(Recti(anchorX - 200, -6, 400, 48), global::mOptionsTopBar, Color(1.0f, 1.0f, 1.0f, alpha));

		const int py = 4;
		const auto& entry = mTabMenuEntries[0];
		const bool isSelected = (mActiveMenu == &mTabMenuEntries);
		const Color color = isSelected ? Color(1.0f, 1.0f, 0.0f, alpha) : Color(1.0f, 1.0f, 1.0f, alpha);
		const bool canGoLeft  = (entry.mSelectedIndex > 0);
		const bool canGoRight = (entry.mSelectedIndex < entry.mOptions.size() - 1);

		const int center = anchorX;
		int arrowDistance = 100;
		if (isSelected)
		{
			const int offset = (int)std::fmod(FTX::getTime() * 6.0f, 6.0f);
			arrowDistance += ((offset > 3) ? (6 - offset) : offset);
		}

		for (size_t k = 0; k < entry.mOptions.size(); ++k)
		{
			const Color color2 = (k == entry.mSelectedIndex) ? color : Color(0.9f, 0.9f, 0.9f, alpha * 0.8f);
			const int px = roundToInt(((float)k - mActiveTabAnimated) * 220.0f) + center - 80;
			drawer.printText(global::mSonicFontC, Recti(px, py, 160, 20), entry.mOptions[k].mText, 5, color2);
		}
		if (canGoLeft)
			drawer.printText(global::mOxyfontRegular, Recti(center - arrowDistance, py + 6, 0, 10), "<", 5, color);
		if (canGoRight)
			drawer.printText(global::mOxyfontRegular, Recti(center + arrowDistance, py + 6, 0, 10), ">", 5, color);

		if (isSelected)
			mScrolling.setCurrentSelection(0, py + 36);
	}

	if (mState == State::CONFIRM)
		renderConfirmation(alpha);

	drawer.performRendering();
}

void N3DSMenu::renderConfirmation(float alpha)
{
	Drawer& drawer = EngineMain::instance().getDrawer();
	Item item;
	if (!getItem(mActiveTab, mActiveMenu->selected().mData, item))
		return;

	// Darken everything, then a box in the middle
	drawer.drawRect(Recti(0, 0, (int)mRect.width, (int)mRect.height), Color(0.0f, 0.0f, 0.0f, 0.55f * alpha));
	const Recti box(60, 72, 280, 80);
	drawer.drawRect(Recti(box.x - 2, box.y - 2, box.width + 4, box.height + 4), Color(0.6f, 0.8f, 1.0f, alpha));
	drawer.drawRect(box, Color(0.02f, 0.1f, 0.35f, alpha));

	String title(0, "Buy %s?", item.mName.c_str());
	drawer.printText(global::mSonicFontB, Recti(200, box.y + 10, 0, 10), title, 5, Color(1.0f, 1.0f, 1.0f, alpha));
	drawer.printText(global::mOxyfontRegular, Recti(200, box.y + 30, 0, 10), String(0, "%d Play Coins  (you have %d)", item.mPrice, mPlayCoins), 5, Color(1.0f, 0.85f, 0.2f, alpha));

	const Color blink(1.0f, 1.0f - std::fabs(std::fmod(FTX::getTime(), 1.0f) - 0.5f) * 2.0f, 0.0f, alpha);
	drawer.printText(global::mOxyfontSmall, Recti(200, box.y + 56, 0, 10), "A: Buy        B: Cancel", 5, blink);
}

void N3DSMenu::goBack()
{
	playMenuSound(0xad);
	mMenuBackground->openMainMenu();
	mState = State::FADE_TO_MENU;
	updateThemePreview();		// Back to the active theme on the bottom screen
}

#endif
