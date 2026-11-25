/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the GameBanana mod browser in the Mods menu (the network side is in N3DSGameBanana).
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include "oxygen/drawing/DrawerTexture.h"
#include "oxygen/platform/3ds/N3DSGameBanana.h"

#include <map>
#include <memory>

class Drawer;


class GameBananaBrowser
{
public:
	void open(const std::string& modsDirectory, const std::string& workDirectory);
	void close();
	inline bool isOpen() const  { return mOpen; }
	inline bool isBusy() const  { return mState.mInstall == n3ds::gamebanana::Install::DOWNLOADING || mState.mInstall == n3ds::gamebanana::Install::UNPACKING; }

	void update(float timeElapsed);
	void render(Drawer& drawer);

	// Mods that got installed, for the Mods menu to turn on (once each)
	bool takeInstalledMod(std::string& outFolder);
	inline int getInstalls() const  { return mInstalls; }

private:
	struct Picture
	{
		DrawerTexture mTexture;
		bool mRequested = false;
		bool mLoaded = false;
		bool mFailed = false;
		float mAge = 0.0f;			// For a short fade-in
	};

	enum class View
	{
		LIST,
		DETAILS
	};

private:
	void updateList(float timeElapsed);
	void updateDetails(float timeElapsed);
	void updateInstall();
	void receivePictures(float timeElapsed);
	void requestPage(n3ds::gamebanana::Listing listing, int page);
	void askForSearch();
	void openDetails(const n3ds::gamebanana::ModSummary& mod);
	void startInstall();
	Picture* getPicture(const std::string& url, int maxWidth);

	void renderList(Drawer& drawer, float alpha);
	void renderDetails(Drawer& drawer, float alpha);
	void renderInstall(Drawer& drawer, float alpha);
	void renderPicture(Drawer& drawer, const std::string& url, int x, int y, int width, int height, float alpha);
	void wrapDescription();

private:
	bool mOpen = false;
	float mTime = 0.0f;
	bool mOnline = true;
	int mInstalls = 0;
	n3ds::gamebanana::State mState;
	View mView = View::LIST;

	// List
	n3ds::gamebanana::Listing mListing = n3ds::gamebanana::Listing::POPULAR;
	std::string mSearch;
	int mPage = 1;
	int mSelected = 0;
	int mScroll = 0;
	float mScrollAnimated = 0.0f;

	// Details
	n3ds::gamebanana::ModSummary mDetailsMod;		// What's known from the list already, while the details load
	int mSelectedFile = 0;
	int mDescriptionScroll = 0;
	std::vector<std::wstring> mDescriptionLines;
	uint32_t mWrappedFor = 0;
	bool mConfirmFlagged = false;					// Asked once more for a file GameBanana's check didn't call clean
	int mPictureIndex = 0;							// Which of the mod's preview pictures is shown
	float mPictureTime = 0.0f;

	std::map<std::string, std::unique_ptr<Picture>> mPictures;
};

#endif
