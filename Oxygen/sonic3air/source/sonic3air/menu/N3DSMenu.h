/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the "3DS" menu, where Play Coins can be spent on bottom screen themes, secrets and a starting shield.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include "sonic3air/menu/GameMenuBase.h"
#include "oxygen/drawing/DrawerTexture.h"
#include "oxygen/platform/3ds/N3DSShop.h"

class MenuBackground;


class N3DSMenu : public GameMenuBase
{
public:
	N3DSMenu(MenuBackground& menuBackground);
	~N3DSMenu();

	virtual BaseState getBaseState() const override;
	virtual void setBaseState(BaseState baseState) override;
	virtual void onFadeIn() override;
	virtual bool canBeRemoved() override;

	virtual void initialize() override;
	virtual void deinitialize() override;
	virtual void keyboard(const rmx::KeyboardEvent& ev) override;
	virtual void update(float timeElapsed) override;
	virtual void render() override;

private:
	enum class State
	{
		INACTIVE,
		APPEAR,
		SHOW,
		CONFIRM,		// Asking whether to buy the selected item
		FADE_TO_MENU
	};

	struct Tab
	{
		enum Id
		{
			THEMES	= 0,
			SECRETS	= 1,
			SHIELD	= 2
		};
		GameMenuEntries mMenuEntries;
	};

	struct Item
	{
		std::string mName;
		int mPrice = 0;
		bool mOwned = false;
	};

private:
	void rebuildTabs();
	void goBack();
	void onAccept();
	void buySelected();
	void updateThemePreview();
	bool getItem(size_t tabIndex, uint32 data, Item& outItem) const;
	void showMessage(const std::string& text, bool isError);
	void renderConfirmation(float alpha);
	void createIcons();

	// Reading from the SD card and system storage happens on the background thread (it can take a second at times)
	struct CustomThemeThumbnail
	{
		std::string mName;				// File name (UTF-8)
		long long mSize = 0;			// For telling whether the file changed since the last scan
		long long mModified = 0;
		std::vector<uint32> mPixels;	// 64 x 48, ABGR
	};
	void startBackgroundLoading();
	static void scanCustomThemesFolder(const std::vector<CustomThemeThumbnail>& previous, std::vector<CustomThemeThumbnail>& outThemes);
	void applyCustomThemes(std::vector<CustomThemeThumbnail>&& themes);

private:
	MenuBackground* mMenuBackground = nullptr;

	GameMenuEntries mTabMenuEntries;
	Tab mTabs[3];
	size_t mActiveTab = 0;
	float mActiveTabAnimated = 0.0f;
	GameMenuEntries* mActiveMenu = &mTabMenuEntries;

	State mState = State::INACTIVE;
	GameMenuScrolling mScrolling;

	int mPlayCoins = -1;			// -1 if they could not be read
	bool mPlayCoinsLoading = false;	// Still being read in the background
	std::string mPreviewedTheme;	// Theme currently shown on the bottom screen as preview ("t<number>" or "c<file name>")

	// The player's own background pictures from the themes folder on the SD card
	static const int MAX_CUSTOM_THEMES = 12;
	std::vector<std::string> mCustomThemes;		// File names (UTF-8)
	DrawerTexture mCustomIcons[MAX_CUSTOM_THEMES];	// Thumbnails
	std::vector<CustomThemeThumbnail> mThumbnailCache;	// From the last scan, so unchanged pictures aren't decoded again
	bool mScanRunning = false;

	std::string mMessage;			// Short feedback at the bottom, like "Not enough Play Coins"
	bool mMessageIsError = false;
	float mMessageTimeout = 0.0f;

	// Pixel art icons of the themes (in each theme's two colors), and the game's monitors for the shields
	bool mIconsCreated = false;
	DrawerTexture mThemeIcons[n3ds::shop::_NUM_THEMES];		// Sized by the theme count (a fixed size here once overflowed when themes were added)
	DrawerTexture mShieldIcons[n3ds::shop::_NUM_SHIELDS];
	bool mHasShieldIcons = false;

	// Sonic's move sprites for the Drop Dash and Super Peel-Out secrets (animated)
	DrawerTexture mDropDashFrames[2];
	DrawerTexture mPeelOutFrames[4];
	bool mHasMoveSprites = false;
};

#endif
