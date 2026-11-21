// Host test: picture loading isn't needed (the player's own theme pictures aren't tested), it always fails
#pragma once

#include <string>
#include <cstdint>

class Bitmap
{
public:
	int getWidth() const  { return 0; }
	int getHeight() const { return 0; }
	uint32_t* getPixelPointer(int, int) { return &mDummy; }
private:
	uint32_t mDummy = 0;
};

namespace FileHelper
{
	inline bool loadBitmap(Bitmap&, const std::wstring&, bool) { return false; }
}
