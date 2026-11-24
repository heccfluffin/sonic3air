/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the GameBanana mod browser in the Mods menu (see the header).
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"

#if defined(PLATFORM_3DS)

#include "sonic3air/menu/mods/N3DSGameBananaBrowser.h"
#include "sonic3air/menu/GameMenuBase.h"
#include "sonic3air/menu/SharedResources.h"

#include "oxygen/application/input/InputManager.h"
#include "oxygen/drawing/Drawer.h"
#include "oxygen_netcore/network/Sockets.h"

#include <3ds.h>
#include <ctime>
#include <unistd.h>


namespace
{
	using namespace n3ds::gamebanana;

	const int ROW_HEIGHT = 35;
	const int VISIBLE_ROWS = 5;
	const int LIST_TOP = 27;
	const int THUMB_WIDTH = 64;
	const int THUMB_HEIGHT = 35;
	const int SCREEN_HEIGHT = 224;		// The game's screen (like the Mega Drive's), shown scaled up on the 3DS
	const int FOOTER_Y = SCREEN_HEIGHT - 13;
	const size_t MAX_PICTURES = 60;

	const char* const LISTING_NAMES[] = { "Popular", "Newest", "Top rated", "Search" };

	Color withAlpha(Color color, float alpha)
	{
		color.a *= alpha;
		return color;
	}

	// Shortened with "..." to fit the width
	std::string fitText(Font& font, const std::string& text, int maxWidth)
	{
		if (font.getWidth(text) <= maxWidth)
			return text;
		std::string result = text;
		while (!result.empty() && font.getWidth(result + "...") > maxWidth)
			result.pop_back();
		while (!result.empty() && result.back() == ' ')
			result.pop_back();
		return result + "...";
	}

	std::string formatCount(int value)
	{
		if (value >= 1000000)
			return std::to_string(value / 1000000) + "." + std::to_string(value / 100000 % 10) + "M";
		if (value >= 10000)
			return std::to_string(value / 1000) + "k";
		if (value >= 1000)
			return std::to_string(value / 1000) + "." + std::to_string(value / 100 % 10) + "k";
		return std::to_string(value);
	}

	std::string formatSize(uint64_t bytes)
	{
		if (bytes >= 1024 * 1024)
			return std::to_string((int)(bytes / (1024 * 1024))) + "." + std::to_string((int)(bytes % (1024 * 1024) * 10 / (1024 * 1024))) + " MB";
		return std::to_string((int)((bytes + 1023) / 1024)) + " KB";
	}

	std::string formatDate(int64_t unixTime)
	{
		if (unixTime <= 0)
			return "";
		static const char* const MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
		const time_t t = (time_t)unixTime;
		const tm* date = gmtime(&t);
		if (nullptr == date)
			return "";
		return std::to_string(date->tm_mday) + " " + MONTHS[date->tm_mon % 12] + " " + std::to_string(date->tm_year + 1900);
	}

	void drawPanelFrame(Drawer& drawer, const Recti& rect, float alpha)
	{
		drawer.drawRect(Rectf(rect), Color(0.07f, 0.10f, 0.22f, 0.97f * alpha));
		drawer.drawRect(Rectf((float)rect.x, (float)rect.y, (float)rect.width, 2.0f), Color(1.0f, 0.82f, 0.2f, alpha));
		drawer.drawRect(Rectf((float)rect.x, (float)(rect.y + rect.height - 2), (float)rect.width, 2.0f), Color(1.0f, 0.82f, 0.2f, alpha));
	}

	void drawProgressBar(Drawer& drawer, const Recti& rect, float fraction, float alpha)
	{
		drawer.drawRect(Rectf(rect), Color(0.16f, 0.2f, 0.36f, alpha));
		const float width = (float)rect.width * clamp(fraction, 0.0f, 1.0f);
		if (width > 0.0f)
			drawer.drawRect(Rectf((float)rect.x, (float)rect.y, width, (float)rect.height), Color(1.0f, 0.8f, 0.2f, alpha));
	}
}


void GameBananaBrowser::open(const std::string& modsDirectory, const std::string& workDirectory)
{
	mOpen = true;
	mTime = 0.0f;
	mInstalls = 0;
	mView = View::LIST;
	mSelected = 0;
	mScroll = 0;
	mScrollAnimated = 0.0f;
	mConfirmFlagged = false;

	Sockets::startupSockets();
#if defined(N3DS_GAMEBANANA_TEST_URL)
	// Only for tests in the emulator, which has no Wi-Fi of its own: a local stand-in for GameBanana
	setBaseUrl(N3DS_GAMEBANANA_TEST_URL);
	mOnline = true;
#else
	mOnline = (gethostid() != 0);		// The 3DS's address in the Wi-Fi, 0 without a connection
#endif
	start(modsDirectory, workDirectory);
	mState = getState();
	if (mOnline && mState.mMods.empty())
		requestPage(mListing, 1);
	RMX_LOG_INFO("GameBanana: browser opened" << (mOnline ? "" : " (not connected)"));
}

void GameBananaBrowser::close()
{
	if (!mOpen)
		return;
	mOpen = false;
	clearThumbnailRequests();
	stop();
	mPictures.clear();
	RMX_LOG_INFO("GameBanana: browser closed");
}

bool GameBananaBrowser::takeInstalledMod(std::string& outFolder)
{
	if (!n3ds::gamebanana::takeInstalledMod(outFolder))
		return false;
	++mInstalls;
	return true;
}

void GameBananaBrowser::update(float timeElapsed)
{
	if (!mOpen)
		return;
	mTime += timeElapsed;
	mState = getState();
	receivePictures(timeElapsed);

	const InputManager::ControllerScheme& keys = InputManager::instance().getController(0);
	if (mTime < 0.2f)
		return;

	if (mState.mInstall != Install::IDLE)
	{
		updateInstall();
		return;
	}

	if (!mOnline)
	{
		if (keys.A.justPressed())
		{
		#if !defined(N3DS_GAMEBANANA_TEST_URL)
			mOnline = (gethostid() != 0);
		#endif
			if (mOnline)
				requestPage(mListing, 1);
			else
				GameMenuBase::playMenuSound(0xb2);
		}
		else if (keys.B.justPressed() || keys.Back.justPressed() || keys.Start.justPressed())
		{
			GameMenuBase::playMenuSound(0xad);
			close();
		}
		return;
	}

	if (mView == View::DETAILS)
		updateDetails(timeElapsed);
	else
		updateList(timeElapsed);
}

void GameBananaBrowser::requestPage(Listing listing, int page)
{
	mListing = listing;
	mPage = std::max(page, 1);
	mSelected = 0;
	mScroll = 0;
	mScrollAnimated = 0.0f;
	requestList(listing, mSearch, mPage);
	mState = getState();
}

void GameBananaBrowser::askForSearch()
{
	// The system keyboard (it takes over both screens until it's closed)
	SwkbdState keyboard;
	swkbdInit(&keyboard, SWKBD_TYPE_WESTERN, 2, 48);
	swkbdSetHintText(&keyboard, "Search Sonic 3 A.I.R. mods on GameBanana");
	swkbdSetInitialText(&keyboard, mSearch.c_str());
	swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	char buffer[160] = { 0 };
	const SwkbdButton button = swkbdInputText(&keyboard, buffer, sizeof(buffer));
	if (button == SWKBD_BUTTON_CONFIRM && buffer[0] != 0)
	{
		mSearch = toDisplayText(buffer);
		GameMenuBase::playMenuSound(0x63);
		requestPage(Listing::SEARCH, 1);
	}
	else if (mListing == Listing::SEARCH && mSearch.empty())
	{
		// Nothing searched for yet: back to the popular ones
		requestPage(Listing::POPULAR, 1);
	}
}

void GameBananaBrowser::updateList(float timeElapsed)
{
	const InputManager::ControllerScheme& keys = InputManager::instance().getController(0);
	const int count = (int)mState.mMods.size();
	const bool ready = (mState.mListLoad == Load::READY);

	if (keys.B.justPressed() || keys.Back.justPressed() || keys.Start.justPressed())
	{
		GameMenuBase::playMenuSound(0xad);
		close();
		return;
	}

	// L / R: which list
	if (keys.L.justPressed() || keys.R.justPressed())
	{
		const int listings = 4;
		const int next = ((int)mListing + (keys.R.justPressed() ? 1 : listings - 1)) % listings;
		GameMenuBase::playMenuSound(0x5b);
		if ((Listing)next == Listing::SEARCH && mSearch.empty())
		{
			mListing = Listing::SEARCH;
			askForSearch();
		}
		else
		{
			requestPage((Listing)next, 1);
		}
		return;
	}

	if (keys.Y.justPressed())
	{
		askForSearch();
		return;
	}

	if (mState.mListLoad == Load::FAILED && keys.A.justPressed())
	{
		GameMenuBase::playMenuSound(0x63);
		requestPage(mListing, mPage);
		return;
	}

	if (ready && count > 0)
	{
		if (keys.Up.justPressedOrRepeat() && mSelected > 0)
		{
			--mSelected;
			GameMenuBase::playMenuSound(0x5b);
		}
		else if (keys.Down.justPressedOrRepeat() && mSelected < count - 1)
		{
			++mSelected;
			GameMenuBase::playMenuSound(0x5b);
		}
		else if (keys.A.justPressed())
		{
			GameMenuBase::playMenuSound(0x63);
			openDetails(mState.mMods[mSelected]);
			return;
		}
	}

	// Left / right: pages
	const int pages = std::max(1, (mState.mTotalMods + mState.mPerPage - 1) / std::max(mState.mPerPage, 1));
	if (keys.Left.justPressed() && mPage > 1)
	{
		GameMenuBase::playMenuSound(0x5b);
		requestPage(mListing, mPage - 1);
		return;
	}
	if (keys.Right.justPressed() && (mPage < pages || !ready))
	{
		if (ready)
		{
			GameMenuBase::playMenuSound(0x5b);
			requestPage(mListing, mPage + 1);
		}
		return;
	}

	// Scrolling follows the selection
	if (mSelected < mScroll)
		mScroll = mSelected;
	else if (mSelected >= mScroll + VISIBLE_ROWS)
		mScroll = mSelected - VISIBLE_ROWS + 1;
	mScrollAnimated += ((float)mScroll - mScrollAnimated) * std::min(1.0f, timeElapsed * 14.0f);

	// The pictures for this page
	if (ready)
	{
		for (int i = 0; i < count; ++i)
		{
			const int row = i - mScroll;
			if (row >= -2 && row < VISIBLE_ROWS + 4)
				getPicture(mState.mMods[i].mThumbnailUrl, THUMB_WIDTH);
		}
	}
}

void GameBananaBrowser::openDetails(const ModSummary& mod)
{
	mDetailsMod = mod;
	mView = View::DETAILS;
	mSelectedFile = 0;
	mDescriptionScroll = 0;
	mDescriptionLines.clear();
	mWrappedFor = 0;
	mConfirmFlagged = false;
	requestDetails(mod.mId);
	mState = getState();
}

void GameBananaBrowser::updateDetails(float timeElapsed)
{
	const InputManager::ControllerScheme& keys = InputManager::instance().getController(0);
	const bool ready = (mState.mDetailsLoad == Load::READY && mState.mDetails.mId == mDetailsMod.mId);

	if (keys.B.justPressed() || keys.Back.justPressed())
	{
		GameMenuBase::playMenuSound(0xad);
		mView = View::LIST;
		return;
	}

	if (mState.mDetailsLoad == Load::FAILED && keys.A.justPressed())
	{
		GameMenuBase::playMenuSound(0x63);
		requestDetails(mDetailsMod.mId);
		return;
	}
	if (!ready)
		return;

	wrapDescription();
	const ModDetails& details = mState.mDetails;
	if (!details.mImageUrls.empty())
		getPicture(details.mImageUrls[0], 160);

	// Up / down: the description, left / right: which file
	const int maxScroll = std::max(0, (int)mDescriptionLines.size() - 6);
	if (keys.Up.justPressedOrRepeat() && mDescriptionScroll > 0)
		--mDescriptionScroll;
	else if (keys.Down.justPressedOrRepeat() && mDescriptionScroll < maxScroll)
		++mDescriptionScroll;

	const int files = (int)details.mFiles.size();
	if (files > 1 && (keys.Left.justPressed() || keys.Right.justPressed()))
	{
		mSelectedFile = (mSelectedFile + (keys.Right.justPressed() ? 1 : files - 1)) % files;
		mConfirmFlagged = false;
		GameMenuBase::playMenuSound(0x5b);
	}

	if (keys.A.justPressed() && files > 0)
	{
		const FileInfo& file = details.mFiles[mSelectedFile];
		if (nullptr != getFileProblem(file))
		{
			GameMenuBase::playMenuSound(0xb2);		// The reason is shown right there
		}
		else if (file.mFlagged && !mConfirmFlagged)
		{
			mConfirmFlagged = true;		// Once more to really install it
			GameMenuBase::playMenuSound(0x5b);
		}
		else
		{
			GameMenuBase::playMenuSound(0x63);
			startInstall();
		}
	}
}

void GameBananaBrowser::startInstall()
{
	const ModDetails& details = mState.mDetails;
	if (mSelectedFile < 0 || mSelectedFile >= (int)details.mFiles.size())
		return;
	requestInstall(details.mFiles[mSelectedFile], details.mName);
	mState = getState();
}

void GameBananaBrowser::updateInstall()
{
	const InputManager::ControllerScheme& keys = InputManager::instance().getController(0);
	if (mState.mInstall == Install::DOWNLOADING)
	{
		if (keys.B.justPressed())
		{
			GameMenuBase::playMenuSound(0xad);
			cancelInstall();
		}
	}
	else if (mState.mInstall == Install::DONE)
	{
		if (keys.A.justPressed() || keys.B.justPressed())
		{
			GameMenuBase::playMenuSound(mState.mInstallSuccess ? 0x63 : 0xad);
			acknowledgeInstall();
			mState = getState();
		}
	}
}

GameBananaBrowser::Picture* GameBananaBrowser::getPicture(const std::string& url, int maxWidth)
{
	if (url.empty())
		return nullptr;
	auto it = mPictures.find(url);
	if (it == mPictures.end())
	{
		if (mPictures.size() >= MAX_PICTURES)
		{
			// Make room: the ones of other pages go first (they're loaded again if needed)
			for (auto it2 = mPictures.begin(); it2 != mPictures.end() && mPictures.size() >= MAX_PICTURES / 2; )
			{
				bool used = false;
				for (const ModSummary& mod : mState.mMods)
					used = used || (mod.mThumbnailUrl == it2->first);
				if (!used && it2->second->mRequested && (it2->second->mLoaded || it2->second->mFailed))
					it2 = mPictures.erase(it2);
				else
					++it2;
			}
		}
		it = mPictures.emplace(url, std::make_unique<Picture>()).first;
	}
	Picture& picture = *it->second;
	if (!picture.mRequested)
	{
		picture.mRequested = true;
		requestThumbnail(url, maxWidth);
	}
	return &picture;
}

void GameBananaBrowser::receivePictures(float timeElapsed)
{
	Thumbnail thumbnail;
	int received = 0;
	while (received < 3 && takeThumbnail(thumbnail))		// A few per frame, to keep it smooth
	{
		++received;
		const auto it = mPictures.find(thumbnail.mUrl);
		if (it == mPictures.end())
			continue;
		Picture& picture = *it->second;
		if (thumbnail.mFailed || thumbnail.mPixels.empty())
		{
			picture.mFailed = true;
			continue;
		}
		Bitmap& bitmap = picture.mTexture.accessBitmap();
		bitmap.create(thumbnail.mWidth, thumbnail.mHeight);
		memcpy(bitmap.getData(), thumbnail.mPixels.data(), thumbnail.mPixels.size() * sizeof(uint32));
		picture.mTexture.bitmapUpdated();
		picture.mLoaded = true;
		picture.mAge = 0.0f;
	}
	for (auto& pair : mPictures)
		pair.second->mAge += timeElapsed;
}

void GameBananaBrowser::wrapDescription()
{
	const ModDetails& details = mState.mDetails;
	if (mWrappedFor == details.mId)
		return;
	mWrappedFor = details.mId;
	mDescriptionLines.clear();
	std::string text = details.mText;
	if (text.empty())
		text = details.mSummary;
	if (text.empty())
		text = "(No description)";
	// Line by line, so empty lines stay
	size_t start = 0;
	while (start <= text.size())
	{
		const size_t end = text.find('\n', start);
		const std::string line = text.substr(start, (end == std::string::npos) ? std::string::npos : end - start);
		if (line.empty())
		{
			mDescriptionLines.emplace_back();
		}
		else
		{
			std::vector<std::wstring> wrapped;
			global::mOxyfontSmall.wordWrapText(wrapped, 376, String(line));
			for (std::wstring& part : wrapped)
				mDescriptionLines.push_back(part);
		}
		if (end == std::string::npos || mDescriptionLines.size() > 400)
			break;
		start = end + 1;
	}
}

void GameBananaBrowser::render(Drawer& drawer)
{
	if (!mOpen)
		return;
	const float alpha = std::min(mTime / 0.15f, 1.0f);
	drawer.drawRect(Rectf(0.0f, 0.0f, 400.0f, (float)SCREEN_HEIGHT), Color(0.0f, 0.0f, 0.0f, 0.7f * alpha));
	drawPanelFrame(drawer, Recti(0, 0, 400, SCREEN_HEIGHT), alpha);

	if (mView == View::DETAILS && mOnline)
		renderDetails(drawer, alpha);
	else
		renderList(drawer, alpha);

	if (mState.mInstall != Install::IDLE)
		renderInstall(drawer, alpha);
}

void GameBananaBrowser::renderPicture(Drawer& drawer, const std::string& url, int x, int y, int width, int height, float alpha)
{
	const auto it = url.empty() ? mPictures.end() : mPictures.find(url);
	const Picture* picture = (it == mPictures.end()) ? nullptr : it->second.get();
	if (nullptr != picture && picture->mLoaded)
	{
		const float fade = std::min(picture->mAge / 0.2f, 1.0f);
		drawer.drawRect(Rectf((float)x, (float)y, (float)width, (float)height), Color(0.12f, 0.16f, 0.3f, alpha));
		drawer.drawRect(Rectf((float)x, (float)y, (float)width, (float)height), const_cast<DrawerTexture&>(picture->mTexture), Color(1.0f, 1.0f, 1.0f, alpha * fade));
	}
	else
	{
		drawer.drawRect(Rectf((float)x, (float)y, (float)width, (float)height), Color(0.12f, 0.16f, 0.3f, alpha));
		const bool failed = (nullptr == picture) ? url.empty() : picture->mFailed;
		if (failed)
			drawer.printText(global::mOxyfontTiny, Recti(x, y, width, height), "no picture", 5, Color(0.45f, 0.5f, 0.65f, alpha));
		else
			drawer.printText(global::mOxyfontTiny, Recti(x, y, width, height), std::string((int)(mTime * 3.0f) % 3 + 1, '.'), 5, Color(0.6f, 0.65f, 0.8f, alpha));
	}
}

void GameBananaBrowser::renderList(Drawer& drawer, float alpha)
{
	const Color white(1.0f, 1.0f, 1.0f, alpha);
	const Color gray(0.62f, 0.68f, 0.82f, alpha);
	const Color yellow(1.0f, 0.85f, 0.3f, alpha);

	// Header: title and the lists to choose from
	drawer.printText(global::mSonicFontB, Recti(8, 7, 0, 10), "GAMEBANANA", 1, yellow);
	int tabX = 392;
	for (int i = 3; i >= 0; --i)
	{
		std::string name = LISTING_NAMES[i];
		if (i == (int)Listing::SEARCH && !mSearch.empty())
			name = "\"" + fitText(global::mOxyfontSmall, mSearch, 60) + "\"";
		const int width = global::mOxyfontSmall.getWidth(name) + 10;
		tabX -= width;
		const bool active = (i == (int)mListing);
		if (active)
			drawer.drawRect(Rectf((float)tabX, 5.0f, (float)width, 15.0f), Color(1.0f, 0.82f, 0.2f, 0.9f * alpha));
		drawer.printText(global::mOxyfontSmall, Recti(tabX, 5, width, 15), name, 5, active ? Color(0.1f, 0.1f, 0.15f, alpha) : gray);
		tabX -= 2;
	}
	drawer.printText(global::mOxyfontTiny, Recti(tabX - 26, 5, 24, 15), "L / R", 6, gray);
	drawer.drawRect(Rectf(6.0f, 24.0f, 388.0f, 1.0f), Color(0.3f, 0.4f, 0.7f, alpha));

	if (!mOnline)
	{
		drawer.printText(global::mOxyfontRegular, Recti(24, 60, 0, 10), "The 3DS isn't connected to the Internet.", 1, white);
		drawer.printText(global::mOxyfontSmall, Recti(24, 86, 0, 10), "Turn on wireless (the switch or the HOME Menu) and make sure", 1, gray);
		drawer.printText(global::mOxyfontSmall, Recti(24, 100, 0, 10), "there's an Internet connection set up in System Settings.", 1, gray);
		drawer.printText(global::mOxyfontSmall, Recti(24, 130, 0, 10), "A: try again        B: back", 1, yellow);
		return;
	}

	const std::vector<ModSummary>& mods = mState.mMods;
	if (mState.mListLoad == Load::LOADING || (mState.mListLoad == Load::IDLE && mods.empty()))
	{
		const std::string what = (mListing == Listing::SEARCH) ? "Searching for \"" + mSearch + "\"" : std::string("Loading ") + LISTING_NAMES[(int)mListing] + " mods";
		drawer.printText(global::mOxyfontRegular, Recti(0, 100, 400, 20), what + std::string((int)(mTime * 3.0f) % 4, '.'), 5, white);
		drawer.printText(global::mOxyfontSmall, Recti(0, 124, 400, 14), "from gamebanana.com", 5, gray);
	}
	else if (mState.mListLoad == Load::FAILED)
	{
		drawer.printText(global::mOxyfontRegular, Recti(24, 70, 0, 10), "Couldn't load the mods.", 1, white);
		std::vector<std::wstring> lines;
		global::mOxyfontSmall.wordWrapText(lines, 352, String(mState.mListError));
		for (size_t i = 0; i < lines.size() && i < 4; ++i)
			drawer.printText(global::mOxyfontSmall, Recti(24, 94 + (int)i * 13, 0, 10), WString(lines[i]), 1, gray);
		drawer.printText(global::mOxyfontSmall, Recti(24, 160, 0, 10), "A: try again        B: back", 1, yellow);
	}
	else if (mods.empty())
	{
		drawer.printText(global::mOxyfontRegular, Recti(0, 100, 400, 20), (mListing == Listing::SEARCH) ? "Nothing found for \"" + mSearch + "\"." : std::string("No mods here."), 5, white);
		drawer.printText(global::mOxyfontSmall, Recti(0, 124, 400, 14), "Y: search for something else", 5, gray);
	}
	else
	{
		// The mods, with a little scrolling
		for (int i = 0; i < (int)mods.size(); ++i)
		{
			const float rowY = (float)LIST_TOP + ((float)i - mScrollAnimated) * (float)ROW_HEIGHT;
			if (rowY < (float)LIST_TOP - ROW_HEIGHT + 4 || rowY > (float)(LIST_TOP + VISIBLE_ROWS * ROW_HEIGHT) - 4)
				continue;
			const int y = roundToInt(rowY);
			if (y < LIST_TOP - 2 || y + ROW_HEIGHT > LIST_TOP + VISIBLE_ROWS * ROW_HEIGHT + 2)
				continue;		// Only whole rows (nothing clips the drawing here)
			const ModSummary& mod = mods[i];
			const bool selected = (i == mSelected);
			if (selected)
			{
				drawer.drawRect(Rectf(4.0f, (float)y, 392.0f, (float)ROW_HEIGHT - 2), Color(0.25f, 0.34f, 0.66f, 0.85f * alpha));
				drawer.drawRect(Rectf(4.0f, (float)y, 2.0f, (float)ROW_HEIGHT - 2), Color(1.0f, 0.82f, 0.2f, alpha));
			}
			renderPicture(drawer, mod.mThumbnailUrl, 9, y + 1, THUMB_WIDTH, THUMB_HEIGHT - 2, alpha);

			const int textX = 9 + THUMB_WIDTH + 8;
			const std::string likes = formatCount(mod.mLikes) + " likes";
			const int likesWidth = global::mOxyfontSmall.getWidth(likes);
			drawer.printText(global::mOxyfontRegular, Recti(textX, y + 3, 0, 10), fitText(global::mOxyfontRegular, mod.mName, 384 - textX - likesWidth - 8), 1, selected ? Color(1.0f, 1.0f, 0.85f, alpha) : white);
			drawer.printText(global::mOxyfontSmall, Recti(392 - likesWidth, y + 4, 0, 10), likes, 1, Color(1.0f, 0.62f, 0.7f, alpha));

			std::string info = "by " + mod.mAuthor;
			if (!mod.mCategory.empty())
				info += "  -  " + mod.mCategory;
			drawer.printText(global::mOxyfontSmall, Recti(textX, y + 19, 0, 10), fitText(global::mOxyfontSmall, info, 300 - textX), 1, gray);
			std::string note = mod.mObsolete ? "outdated" : !mod.mHasFiles ? "no download here" : formatCount(mod.mViews) + " views";
			drawer.printText(global::mOxyfontSmall, Recti(392 - global::mOxyfontSmall.getWidth(note), y + 19, 0, 10), note, 1, mod.mObsolete || !mod.mHasFiles ? Color(1.0f, 0.6f, 0.4f, alpha) : gray);
		}

		// Scroll indicator
		if ((int)mods.size() > VISIBLE_ROWS)
		{
			const float total = (float)mods.size();
			const float barHeight = (float)(VISIBLE_ROWS * ROW_HEIGHT) * (float)VISIBLE_ROWS / total;
			const float barY = (float)LIST_TOP + (float)(VISIBLE_ROWS * ROW_HEIGHT) * mScrollAnimated / total;
			drawer.drawRect(Rectf(397.0f, barY, 2.0f, barHeight), Color(1.0f, 0.82f, 0.2f, 0.6f * alpha));
		}
	}

	// Footer: page and controls
	drawer.drawRect(Rectf(6.0f, (float)FOOTER_Y - 3, 388.0f, 1.0f), Color(0.3f, 0.4f, 0.7f, alpha));
	const int pages = std::max(1, (mState.mTotalMods + mState.mPerPage - 1) / std::max(mState.mPerPage, 1));
	if (mState.mListLoad == Load::READY && !mods.empty())
		drawer.printText(global::mOxyfontSmall, Recti(8, FOOTER_Y, 0, 10), "Page " + std::to_string(mPage) + " of " + std::to_string(pages) + "  (left / right)", 1, gray);
	drawer.printText(global::mOxyfontSmall, Recti(392, FOOTER_Y, 0, 10), "A: details   Y: search   B: back", 3, yellow);
}

void GameBananaBrowser::renderDetails(Drawer& drawer, float alpha)
{
	const Color white(1.0f, 1.0f, 1.0f, alpha);
	const Color gray(0.62f, 0.68f, 0.82f, alpha);
	const Color yellow(1.0f, 0.85f, 0.3f, alpha);
	const bool ready = (mState.mDetailsLoad == Load::READY && mState.mDetails.mId == mDetailsMod.mId);
	const ModDetails& details = mState.mDetails;

	// Top: picture, name, numbers
	const std::string pictureUrl = (ready && !details.mImageUrls.empty()) ? details.mImageUrls[0] : std::string();
	renderPicture(drawer, ready ? pictureUrl : mDetailsMod.mThumbnailUrl, 8, 8, 144, 81, alpha);

	const std::string& name = ready ? details.mName : mDetailsMod.mName;
	std::vector<std::wstring> nameLines;
	global::mOxyfontRegular.wordWrapText(nameLines, 232, String(name));
	int y = 9;
	for (size_t i = 0; i < nameLines.size() && i < 2; ++i, y += 15)
		drawer.printText(global::mOxyfontRegular, Recti(160, y, 0, 10), WString(nameLines[i]), 1, white);
	drawer.printText(global::mOxyfontSmall, Recti(160, y + 2, 0, 10), fitText(global::mOxyfontSmall, "by " + (ready ? details.mAuthor : mDetailsMod.mAuthor), 232), 1, gray);
	y += 16;

	if (!ready)
	{
		if (mState.mDetailsLoad == Load::FAILED)
		{
			std::vector<std::wstring> lines;
			global::mOxyfontSmall.wordWrapText(lines, 376, String(mState.mDetailsError));
			for (size_t i = 0; i < lines.size() && i < 3; ++i)
				drawer.printText(global::mOxyfontSmall, Recti(12, 104 + (int)i * 13, 0, 10), WString(lines[i]), 1, gray);
			drawer.printText(global::mOxyfontSmall, Recti(392, FOOTER_Y, 0, 10), "A: try again   B: back", 3, yellow);
		}
		else
		{
			drawer.printText(global::mOxyfontRegular, Recti(0, 130, 400, 20), "Loading the details" + std::string((int)(mTime * 3.0f) % 4, '.'), 5, white);
			drawer.printText(global::mOxyfontSmall, Recti(392, FOOTER_Y, 0, 10), "B: back", 3, yellow);
		}
		return;
	}

	std::string line1 = details.mCategory;
	if (!details.mVersion.empty())
		line1 += (line1.empty() ? "" : "  -  ") + std::string("version ") + details.mVersion;
	drawer.printText(global::mOxyfontSmall, Recti(160, y, 0, 10), fitText(global::mOxyfontSmall, line1, 232), 1, gray);
	y += 13;
	drawer.printText(global::mOxyfontSmall, Recti(160, y, 0, 10), formatCount(details.mLikes) + " likes   " + formatCount(details.mDownloads) + " downloads   " + formatCount(details.mViews) + " views", 1, Color(1.0f, 0.62f, 0.7f, alpha));
	y += 13;
	drawer.printText(global::mOxyfontSmall, Recti(160, y, 0, 10), "Updated " + formatDate(details.mDateUpdated), 1, gray);
	if (details.mObsolete)
		drawer.printText(global::mOxyfontSmall, Recti(392, y, 0, 10), "marked outdated", 3, Color(1.0f, 0.6f, 0.4f, alpha));

	// Middle: the description, scrollable
	const int descTop = 95;
	const int descLines = 6;
	drawer.drawRect(Rectf(6.0f, (float)descTop - 3, 388.0f, 1.0f), Color(0.3f, 0.4f, 0.7f, alpha));
	for (int i = 0; i < descLines && mDescriptionScroll + i < (int)mDescriptionLines.size(); ++i)
		drawer.printText(global::mOxyfontSmall, Recti(12, descTop + i * 12, 0, 10), WString(mDescriptionLines[mDescriptionScroll + i]), 1, Color(0.86f, 0.9f, 1.0f, alpha));
	if ((int)mDescriptionLines.size() > descLines)
	{
		const float total = (float)mDescriptionLines.size();
		const float height = (float)(descLines * 12);
		drawer.drawRect(Rectf(396.0f, (float)descTop + height * (float)mDescriptionScroll / total, 2.0f, height * (float)descLines / total), Color(1.0f, 0.82f, 0.2f, 0.6f * alpha));
	}

	// Bottom: the file to install
	const int fileTop = descTop + descLines * 12 + 4;
	drawer.drawRect(Rectf(6.0f, (float)fileTop, 388.0f, 1.0f), Color(0.3f, 0.4f, 0.7f, alpha));
	if (details.mFiles.empty())
	{
		drawer.printText(global::mOxyfontSmall, Recti(12, fileTop + 6, 0, 10), "No downloads on GameBanana for this one (maybe it's hosted elsewhere).", 1, Color(1.0f, 0.6f, 0.4f, alpha));
		drawer.printText(global::mOxyfontSmall, Recti(392, FOOTER_Y, 0, 10), "Up / down: scroll   B: back", 3, yellow);
		return;
	}
	const FileInfo& file = details.mFiles[std::clamp(mSelectedFile, 0, (int)details.mFiles.size() - 1)];
	std::string fileLabel = (details.mFiles.size() > 1) ? "File " + std::to_string(mSelectedFile + 1) + " of " + std::to_string(details.mFiles.size()) + ": " : std::string("File: ");
	fileLabel += file.mName + "  (" + formatSize(file.mSize) + ((mSelectedFile == 0 && details.mFiles.size() > 1) ? ", newest" : "") + ")";
	drawer.printText(global::mOxyfontSmall, Recti(12, fileTop + 5, 0, 10), fitText(global::mOxyfontSmall, fileLabel, 376), 1, white);

	const char* problem = getFileProblem(file);
	std::string status;
	Color statusColor = gray;
	if (nullptr != problem)
	{
		status = problem;
		statusColor = Color(1.0f, 0.6f, 0.4f, alpha);
	}
	else if (file.mFlagged)
	{
		status = mConfirmFlagged ? "GameBanana's file check didn't call this file clean. Press A again to install it anyway." : "Careful: GameBanana's file check didn't call this file clean.";
		statusColor = Color(1.0f, 0.6f, 0.4f, alpha);
	}
	else if (!file.mDescription.empty())
	{
		status = file.mDescription;
	}
	if (!status.empty())
	{
		std::vector<std::wstring> lines;
		global::mOxyfontSmall.wordWrapText(lines, 376, String(status));
		for (size_t i = 0; i < lines.size() && i < 2; ++i)
			drawer.printText(global::mOxyfontSmall, Recti(12, fileTop + 18 + (int)i * 11, 0, 10), WString(lines[i]), 1, statusColor);
	}

	std::string controls = (nullptr == problem) ? "A: install   " : "";
	if (details.mFiles.size() > 1)
		controls += "Left / right: file   ";
	controls += "B: back";
	drawer.printText(global::mOxyfontSmall, Recti(392, FOOTER_Y, 0, 10), controls, 3, yellow);
}

void GameBananaBrowser::renderInstall(Drawer& drawer, float alpha)
{
	const Color white(1.0f, 1.0f, 1.0f, alpha);
	const Color gray(0.7f, 0.76f, 0.9f, alpha);
	const Color yellow(1.0f, 0.85f, 0.3f, alpha);

	drawer.drawRect(Rectf(0.0f, 0.0f, 400.0f, (float)SCREEN_HEIGHT), Color(0.0f, 0.0f, 0.0f, 0.55f * alpha));
	const Recti box(30, 42, 340, 140);
	drawPanelFrame(drawer, box, alpha);
	drawer.printText(global::mOxyfontRegular, Recti(box.x + 14, box.y + 12, 0, 10), fitText(global::mOxyfontRegular, mState.mInstallName, box.width - 28), 1, white);

	if (mState.mInstall == Install::DOWNLOADING)
	{
		const float fraction = (mState.mTotalBytes > 0) ? (float)((double)mState.mBytes / (double)mState.mTotalBytes) : 0.0f;
		drawer.printText(global::mOxyfontSmall, Recti(box.x + 14, box.y + 40, 0, 10), "Downloading from GameBanana" + std::string((int)(mTime * 3.0f) % 4, '.'), 1, gray);
		drawProgressBar(drawer, Recti(box.x + 14, box.y + 60, box.width - 28, 10), fraction, alpha);
		drawer.printText(global::mOxyfontSmall, Recti(box.x + 14, box.y + 78, 0, 10), formatSize(mState.mBytes) + " of " + formatSize(mState.mTotalBytes), 1, gray);
		drawer.printText(global::mOxyfontSmall, Recti(box.x + box.width - 14, box.y + box.height - 20, 0, 10), "B: cancel", 3, yellow);
	}
	else if (mState.mInstall == Install::UNPACKING)
	{
		const float fraction = (mState.mFilesTotal > 0) ? (float)mState.mFilesDone / (float)mState.mFilesTotal : 0.0f;
		drawer.printText(global::mOxyfontSmall, Recti(box.x + 14, box.y + 40, 0, 10), "Unpacking to the SD card" + std::string((int)(mTime * 3.0f) % 4, '.'), 1, gray);
		drawProgressBar(drawer, Recti(box.x + 14, box.y + 60, box.width - 28, 10), fraction, alpha);
		drawer.printText(global::mOxyfontSmall, Recti(box.x + 14, box.y + 78, 0, 10), std::to_string(mState.mFilesDone) + " of " + std::to_string(mState.mFilesTotal) + " files - please wait", 1, gray);
	}
	else if (mState.mInstall == Install::DONE)
	{
		std::vector<std::wstring> lines;
		global::mOxyfontSmall.wordWrapText(lines, box.width - 28, String(mState.mInstallMessage));
		int y = box.y + 36;
		for (size_t i = 0; i < lines.size() && i < 4; ++i, y += 12)
			drawer.printText(global::mOxyfontSmall, Recti(box.x + 14, y, 0, 10), WString(lines[i]), 1, mState.mInstallSuccess ? Color(0.6f, 1.0f, 0.6f, alpha) : Color(1.0f, 0.6f, 0.5f, alpha));
		y += 4;
		for (const std::string& warning : mState.mInstallWarnings)
		{
			lines.clear();
			global::mOxyfontSmall.wordWrapText(lines, box.width - 28, String(warning));
			for (size_t i = 0; i < lines.size() && y < box.y + box.height - 30; ++i, y += 12)
				drawer.printText(global::mOxyfontSmall, Recti(box.x + 14, y, 0, 10), WString(lines[i]), 1, Color(1.0f, 0.85f, 0.45f, alpha));
		}
		drawer.printText(global::mOxyfontSmall, Recti(box.x + box.width - 14, box.y + box.height - 20, 0, 10), "A: OK", 3, yellow);
	}
}

#endif
