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
#include "oxygen/platform/3ds/N3DSChallenges.h"
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

	// Creates the icons (some come from the game data on the SD card) ahead of time, during startup: the SD card sometimes
	// takes seconds to respond, which froze the game when the menu got opened for the first time
	void preload();

private:
	enum class State
	{
		INACTIVE,
		APPEAR,
		SHOW,
		CONFIRM,		// Asking whether to buy the selected item
		POSTCARD,		// Looking at a postcard of the journal, big
		STATS,			// The journal's numbers, all on one page
		FADE_TO_MENU
	};

	struct Tab
	{
		enum Id
		{
			THEMES	= 0,
			SECRETS	= 1,
			SHIELD	= 2,
			JOURNAL	= 3,
			CHALLENGE = 4		// Doctor Robotnik's Daily Challenge
		};
		GameMenuEntries mMenuEntries;
	};

	struct Item
	{
		std::string mName;
		int mPrice = 0;					// What it costs now, with a voucher if there's one
		bool mOwned = false;
		bool mVoucher = false;			// The price includes a voucher
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
	void openPostcard();
	void renderPostcard(float alpha);
	void renderStats(float alpha);
	void createRobotnik();
	void updateJournalAnimations(float timeElapsed);
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
	static const int NUM_TABS = 5;
	Tab mTabs[NUM_TABS];
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

	// Journal: the stamps (the game's monitors with an icon), an unopened one, and icons for the numbers
	static const int NUM_JOURNAL_ICONS = 7;
	DrawerTexture mStampIcons[7];
	DrawerTexture mStampEmpty;
	DrawerTexture mJournalIcons[NUM_JOURNAL_ICONS];
	bool mHasJournalIcons = false;

	// Sonic's move sprites for the Drop Dash and Super Peel-Out secrets (animated)
	DrawerTexture mDropDashFrames[2];
	DrawerTexture mPeelOutFrames[4];
	bool mHasMoveSprites = false;

	// The postcard being looked at: its view of the zone picture, doubled in size when drawn
	DrawerTexture mPostcardPicture;
	bool mHasPostcardPicture = false;
	DrawerTexture mAlbumFunnyPicture;		// The funny postcard shown in the album, if any
	std::string mAlbumFunnyName;			// Which one is loaded into it
	bool mHasAlbumFunnyPicture = false;

	// Little animations in the journal, played when its tab is first shown after the menu opened: the day's new stamp
	// pops open, and a new postcard drops into the album (also the welcome postcard, the first time the journal is opened)
	bool mPendingStampAnimation = false;
	bool mPendingPostcardAnimation = false;
	float mJournalAnimationTime = -1.0f;	// Seconds since the animations started, -1 when none plays
	float mStampAnimationStart = -1.0f;		// When each one starts in that time, -1 if it's not part of it
	float mPostcardAnimationStart = -1.0f;
	bool mStampSoundPlayed = false;
	bool mPostcardSoundPlayed = false;

	// Doctor Robotnik in his Egg Mobile, drawn from the player's ROM when the challenge tab is first shown: head talking (2 frames),
	// hurt and defeated; and the game's monitors for the challenges (with his face, and broken once beaten)
	DrawerTexture mRobotnik[4];
	bool mRobotnikCreated = false;
	bool mHasRobotnik = false;
	DrawerTexture mMedals[(int)n3ds::challenges::Metal::_NUM];		// The Robotnik medal in each metal, as it levels up
	bool mHasMedals = false;
	DrawerTexture mEggmanMonitor;
	DrawerTexture mBrokenMonitor;
	std::string mRobotnikLine;			// What he says right now, to notice when it changes
	float mRobotnikTalkStart = 0.0f;	// He moves his mouth for a moment after that
	int mRobotnikLaughBounce = 0;		// While he laughs, the Egg Mobile hops a little
};

#endif
