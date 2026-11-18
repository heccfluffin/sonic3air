/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/platform/3ds/N3DSProfiler.h"
#include "oxygen/rendering/software/SoftwareRenderer.h"
#include "oxygen/rendering/software/SoftwareBlur.h"
#include "oxygen/rendering/Geometry.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/drawing/Drawer.h"
#include "oxygen/drawing/DrawerTexture.h"
#include "oxygen/drawing/software/BlitterHelper.h"

#if defined(PLATFORM_VITA)
	#include <psp2/kernel/clib.h>
#endif


namespace detail
{
	class PixelBlockWriter
	{
	public:
		PixelBlockWriter(SoftwareRenderer::BufferedPlaneData& data, const PatternManager::CacheItem* patternCache) :
			mBufferedPlaneData(&data),
			mContent(&data.mContent[0]),
			mPatternCache(patternCache)
		{}

		void newLine(int lineNumber, int position, int paletteIndex)
		{
			mLineNumber = lineNumber;
			mPosition = position;
			mPaletteIndex = paletteIndex;
			mLastPatternBits = 0xffff;
		}

		FORCE_INLINE void addPixels(int x, uint16 patternIndex, int pixels)
		{
			const PatternManager::CacheItem::Pattern& pattern = mPatternCache[patternIndex & 0x07ff].mFlipVariation[(patternIndex >> 11) & 3];
			uint8* dst = &mContent[mPosition + x];
			const uint8* srcPatternPixels = &pattern.mPixels[mPatternPixelOffset];
			memcpy(dst, srcPatternPixels, pixels);

			const uint16 patternBits = (patternIndex & 0xe000);		// Includes priority bit and atex
			if (mLastPatternBits != patternBits)
			{
				mLastPatternBits = patternBits;

				mCurrentPixelBlock = &vectorAdd((patternBits & 0x8000) ? mBufferedPlaneData->mPrioBlocks : mBufferedPlaneData->mNonPrioBlocks);
				mCurrentPixelBlock->mStartCoords.set(x, mLineNumber);
				mCurrentPixelBlock->mLinearPosition = mPosition + x;
				mCurrentPixelBlock->mNumPixels = pixels;
				mCurrentPixelBlock->mAtex = (patternIndex >> 9) & 0x30;
				mCurrentPixelBlock->mPaletteIndex = mPaletteIndex;
			}
			else
			{
				mCurrentPixelBlock->mNumPixels += pixels;
			}
		}

		FORCE_INLINE void addPixels8(int x, uint16 patternIndex)
		{
			// Same as above, but with hardcoded "pixels == 8"
			const PatternManager::CacheItem::Pattern& pattern = mPatternCache[patternIndex & 0x07ff].mFlipVariation[(patternIndex >> 11) & 3];
			uint64* dst = (uint64*)&mContent[mPosition + x];
			const uint64* srcPatternPixels = (uint64*)&pattern.mPixels[mPatternPixelOffset];

		#if defined(PLATFORM_3DS)
			// Like on Vita: the destination is usually not aligned, and the 3DS faults on unaligned 64-bit loads / stores
			memcpy(dst, srcPatternPixels, sizeof(*dst));
		#elif !defined(PLATFORM_VITA)
			*dst = *srcPatternPixels;
		#else
			// This fixes a crash
			sceClibMemcpy(dst, srcPatternPixels, sizeof(*dst));
		#endif

			const uint16 patternBits = (patternIndex & 0xe000);		// Includes priority bit and atex
			if (mLastPatternBits != patternBits)
			{
				mLastPatternBits = patternBits;

				mCurrentPixelBlock = &vectorAdd((patternBits & 0x8000) ? mBufferedPlaneData->mPrioBlocks : mBufferedPlaneData->mNonPrioBlocks);
				mCurrentPixelBlock->mStartCoords.set(x, mLineNumber);
				mCurrentPixelBlock->mLinearPosition = mPosition + x;
				mCurrentPixelBlock->mNumPixels = 8;
				mCurrentPixelBlock->mAtex = (patternIndex >> 9) & 0x30;
				mCurrentPixelBlock->mPaletteIndex = mPaletteIndex;
			}
			else
			{
				mCurrentPixelBlock->mNumPixels += 8;
			}
		}

	public:
		int mPatternPixelOffset = 0;

	private:
		SoftwareRenderer::BufferedPlaneData* mBufferedPlaneData = nullptr;
		uint8* mContent = nullptr;
		const PatternManager::CacheItem* mPatternCache = nullptr;

		int mLineNumber = 0;
		int mPosition = 0;
		int mDepthPosition = 0;
		int mPaletteIndex = 0;

		SoftwareRenderer::BufferedPlaneData::PixelBlock* mCurrentPixelBlock = nullptr;
		uint16 mLastPatternBits = 0xffff;
	};

	// Writes plane pixels to the game screen: palette lookup for each pixel, skipping transparent ones (color index 0 of a palette line).
	// Same result as a simple per-pixel loop, but processes 4 pixels at once where possible:
	//  - The color indices of 4 pixels are read with a single aligned 32-bit load
	//  - If all 4 are opaque (the common case for backgrounds), they get written without per-pixel checks,
	//    which lets an in-order CPU like the 3DS's ARM11 overlap the loads instead of waiting on each one
	template<bool WRITE_DEPTH>
	FORCE_INLINE void writePlanePixel(const uint8* RESTRICT src, uint32* RESTRICT dstRGBA, uint8* RESTRICT dstDepth, const uint32* RESTRICT palette, int i)
	{
		const uint8 colorIndex = src[i];
		if (colorIndex & 0x0f)
		{
			dstRGBA[i] = palette[colorIndex];
			if (WRITE_DEPTH)
				dstDepth[i] = 0x80;
		}
	}

	template<bool WRITE_DEPTH>
	FORCE_INLINE void writePlanePixels(const uint8* RESTRICT src, uint32* RESTRICT dstRGBA, uint8* RESTRICT dstDepth, const uint32* RESTRICT palette, int numPixels)
	{
		int i = 0;

		// Single pixels until the source is 4-byte aligned
		const int head = std::min(numPixels, (int)((4 - ((size_t)src & 3)) & 3));
		for (; i < head; ++i)
			writePlanePixel<WRITE_DEPTH>(src, dstRGBA, dstDepth, palette, i);

		// Groups of 4 pixels
		for (; i + 4 <= numPixels; i += 4)
		{
			const uint32 indices = *(const uint32*)__builtin_assume_aligned(&src[i], 4);
			const uint32 nibbles = indices & 0x0f0f0f0f;
			const bool anyTransparent = (((nibbles - 0x01010101) & ~nibbles & 0x80808080) != 0);	// Checks if any of the 4 bytes is zero
			if (!anyTransparent)
			{
				const uint32 color0 = palette[indices & 0xff];
				const uint32 color1 = palette[(indices >> 8) & 0xff];
				const uint32 color2 = palette[(indices >> 16) & 0xff];
				const uint32 color3 = palette[indices >> 24];
				dstRGBA[i]     = color0;
				dstRGBA[i + 1] = color1;
				dstRGBA[i + 2] = color2;
				dstRGBA[i + 3] = color3;
				if (WRITE_DEPTH)
				{
					dstDepth[i]     = 0x80;
					dstDepth[i + 1] = 0x80;
					dstDepth[i + 2] = 0x80;
					dstDepth[i + 3] = 0x80;
				}
			}
			else if (nibbles != 0)
			{
				// Mix of transparent and opaque pixels
				writePlanePixel<WRITE_DEPTH>(src, dstRGBA, dstDepth, palette, i);
				writePlanePixel<WRITE_DEPTH>(src, dstRGBA, dstDepth, palette, i + 1);
				writePlanePixel<WRITE_DEPTH>(src, dstRGBA, dstDepth, palette, i + 2);
				writePlanePixel<WRITE_DEPTH>(src, dstRGBA, dstDepth, palette, i + 3);
			}
			// Otherwise all 4 are transparent, nothing to do
		}

		// Remaining single pixels
		for (; i < numPixels; ++i)
			writePlanePixel<WRITE_DEPTH>(src, dstRGBA, dstDepth, palette, i);
	}
}


SoftwareRenderer::SoftwareRenderer(RenderParts& renderParts, DrawerTexture& outputTexture) :
	Renderer(RENDERER_TYPE_ID, renderParts, outputTexture)
{
}

void SoftwareRenderer::initialize()
{
	mGameResolution = Configuration::instance().mGameScreen;
	mGameScreenTexture.accessBitmap().create(mGameResolution.x, mGameResolution.y);
}

void SoftwareRenderer::reset()
{
	clearGameScreen();
}

void SoftwareRenderer::setGameResolution(const Vec2i& gameResolution)
{
	if (mGameResolution != gameResolution)
	{
		mGameResolution = gameResolution;
		mGameScreenTexture.accessBitmap().create(mGameResolution.x, mGameResolution.y);
	}
}

void SoftwareRenderer::clearGameScreen()
{
	mGameScreenTexture.accessBitmap().clear(0xff000000);
	mGameScreenTexture.bitmapUpdated();
}

void SoftwareRenderer::renderGameScreen(const std::vector<Geometry*>& geometries)
{
	startRendering();
	Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();

	// Clear the screen
	gameScreenBitmap.clear(mRenderParts.getPaletteManager().getBackdropColor());
	memset(mDepthBuffer, 0, sizeof(mDepthBuffer));
	mEmptyDepthBuffer = true;

	mCurrentViewport.set(0, 0, mGameResolution.x, mGameResolution.y);
	mFullViewport = true;

	for (int i = 0; i < MAX_BUFFER_PLANE_DATA; ++i)
	{
		mBufferedPlaneData[i].mValid = false;
	}

	// Check if sprite masking needed
	const bool usingSpriteMask = isUsingSpriteMask(geometries);

	// Render geometries
	{
		uint16 lastRenderQueue = 0xffff;
		for (size_t i = 0; i < geometries.size(); ++i)
		{
			if (!progressRendering())
				break;

			const uint16 renderQueue = geometries[i]->mRenderQueue;
			if (usingSpriteMask && lastRenderQueue < 0x8000 && renderQueue >= 0x8000)
			{
				// Copy planes (needed for sprite masking)
				mGameScreenCopy = gameScreenBitmap;
			}

			renderGeometry(*geometries[i]);
			lastRenderQueue = renderQueue;
		}
	}

	// Set alpha channel to 0xff to make sure nothing gets lost due to alpha test
	{
		uint64* RESTRICT ptr = (uint64*)gameScreenBitmap.getData();
		uint64* RESTRICT end = ptr + gameScreenBitmap.getPixelCount() / 2;
		for (; ptr < end; ++ptr)
		{
			*ptr |= 0xff000000ff000000ull;
		}
	}

	mGameScreenTexture.bitmapUpdated();
}

void SoftwareRenderer::renderDebugDraw(int debugDrawMode, const Recti& rect)
{
	// TODO: This whole function and how GameView interacts with it, could be improved on (it's a bit of a mess...)

	Drawer& drawer = EngineMain::instance().getDrawer();
	Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();
	const Vec2i oldSize = gameScreenBitmap.getSize();

	// First render to palette bitmap
	static PaletteBitmap paletteBitmap;
	const bool highlightPrioPatterns = (FTX::keyState(SDLK_LSHIFT) != 0);
	mRenderParts.getPlaneManager().dumpAsPaletteBitmap(paletteBitmap, debugDrawMode, highlightPrioPatterns);

	const Vec2i bitmapSize = paletteBitmap.getSize();
	mGameScreenTexture.setupAsRenderTarget(bitmapSize.x, bitmapSize.y);
	gameScreenBitmap.create(bitmapSize);

	// Convert from palette bitmap to RGBA
	{
		const uint32* palette = mRenderParts.getPaletteManager().getMainPalette(0).getRawColors();
		const uint8* src = paletteBitmap.getData();
		uint32* dst = gameScreenBitmap.getData();

		for (int k = 0; k < paletteBitmap.getPixelCount(); ++k)
		{
			uint8 index = *src;
			if (index & 0x80)
			{
				*dst = 0xff000000 | ((palette[index & 0x7f] & 0x00fcfcfc) >> 2);
			}
			else
			{
				*dst = 0xff000000 | palette[index];
			}
			++src;
			++dst;
		}
	}
	mGameScreenTexture.bitmapUpdated();

	mCurrentViewport.set(0, 0, bitmapSize.x, bitmapSize.y);
	mFullViewport = true;

	drawer.setWindowRenderTarget(FTX::screenRect());
	drawer.setBlendMode(BlendMode::OPAQUE);
	drawer.drawUpscaledRect(RenderUtils::getLetterBoxRect(rect, (float)bitmapSize.x / (float)bitmapSize.y), mGameScreenTexture);
	drawer.performRendering();

	mGameScreenTexture.setupAsRenderTarget(oldSize.x, oldSize.y);
	gameScreenBitmap.create(oldSize.x, oldSize.y);
}

void SoftwareRenderer::renderGeometry(const Geometry& geometry)
{
	switch (geometry.getType())
	{
		case Geometry::Type::UNDEFINED:
			break;	// This should never happen anyways

		case Geometry::Type::PLANE:
		{
			N3DS_PROFILE_SCOPE(SW_PLANES);
			renderPlane(static_cast<const PlaneGeometry&>(geometry));
			break;
		}

		case Geometry::Type::SPRITE:
		{
			N3DS_PROFILE_SCOPE(SW_SPRITES);
		#if defined(PLATFORM_3DS) && defined(N3DS_PROFILER)
			// Time and count per sprite type, to see which kind of sprites the time goes to
			const SpriteGeometry& spriteGeometry = static_cast<const SpriteGeometry&>(geometry);
			const RenderItem::Type spriteType = spriteGeometry.mSpriteInfo.getType();
			const n3ds::profiler::Section typeSection = (spriteType == RenderItem::Type::VDP_SPRITE) ? n3ds::profiler::SW_SPR_VDP :
														(spriteType == RenderItem::Type::PALETTE_SPRITE) ? n3ds::profiler::SW_SPR_PALETTE :
														(spriteType == RenderItem::Type::COMPONENT_SPRITE) ? n3ds::profiler::SW_SPR_COMPONENT : n3ds::profiler::_NUM;
			if (typeSection != n3ds::profiler::_NUM)
			{
				n3ds::profiler::addCount((typeSection == n3ds::profiler::SW_SPR_VDP) ? n3ds::profiler::COUNT_SPR_VDP :
										 (typeSection == n3ds::profiler::SW_SPR_PALETTE) ? n3ds::profiler::COUNT_SPR_PALETTE : n3ds::profiler::COUNT_SPR_COMPONENT);
				n3ds::profiler::Scope typeScope(typeSection);
				renderSprite(spriteGeometry);
			}
			else
		#endif
			{
				renderSprite(static_cast<const SpriteGeometry&>(geometry));
			}
			break;
		}

		case Geometry::Type::RECT:
		{
			const RectGeometry& rg = static_cast<const RectGeometry&>(geometry);
			const Recti rect = Recti::getIntersection(rg.mRect, mCurrentViewport);
			mBlitter.blitColor(Blitter::OutputWrapper(mGameScreenTexture.accessBitmap(), rect), rg.mColor, BlendMode::ALPHA);
			break;
		}

		case Geometry::Type::TEXTURED_RECT:
		{
			const TexturedRectGeometry& tg = static_cast<const TexturedRectGeometry&>(geometry);
			Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();

			Blitter::Options blitterOptions;
			blitterOptions.mBlendMode = BlendMode::ALPHA;
			blitterOptions.mTintColor = &tg.mTintColor;
			blitterOptions.mAddedColor = &tg.mAddedColor;

			mBlitter.blitSprite(Blitter::OutputWrapper(mGameScreenTexture.accessBitmap(), mCurrentViewport), Blitter::SpriteWrapper(tg.mDrawerTexture.accessBitmap(), Vec2i()), tg.mRect.getPos(), blitterOptions);
			break;
		}

		case Geometry::Type::EFFECT_BLUR:
		{
			const EffectBlurGeometry& ebg = static_cast<const EffectBlurGeometry&>(geometry);
			Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();

			if (ebg.mBlurValue >= 1)
			{
				N3DS_PROFILE_SCOPE(SW_BLUR);
				SoftwareBlur::blurBitmap(gameScreenBitmap, ebg.mBlurValue);
			}
			break;
		}

		case Geometry::Type::VIEWPORT:
		{
			const ViewportGeometry& vg = static_cast<const ViewportGeometry&>(geometry);
			const Recti fullViewport(0, 0, mGameResolution.x, mGameResolution.y);
			mCurrentViewport = fullViewport;
			mCurrentViewport.intersect(vg.mRect);
			mFullViewport = (mCurrentViewport == fullViewport);
			break;
		}
	}
}

void SoftwareRenderer::renderPlane(const PlaneGeometry& geometry)
{
	Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();

	Recti rect(0, 0, mGameResolution.x, mGameResolution.y);
	rect.intersect(geometry.mActiveRect);
	rect.intersect(mCurrentViewport);
	const int minX = rect.x;
	const int maxX = rect.x + rect.width;
	const int minY = rect.y;
	const int maxY = rect.y + rect.height;

	const PlaneManager& planeManager = mRenderParts.getPlaneManager();
	const ScrollOffsetsManager& scrollOffsetsManager = mRenderParts.getScrollOffsetsManager();
	const PaletteManager& paletteManager = mRenderParts.getPaletteManager();

	// Search for already setup buffered plane data fitting this geometry
	int foundFittingBufferedPlaneDataIndex = -1;
	for (int i = 0; i < MAX_BUFFER_PLANE_DATA; ++i)
	{
		const BufferedPlaneData& bufferedPlaneData = mBufferedPlaneData[i];
		if (bufferedPlaneData.mValid &&
			bufferedPlaneData.mPlaneIndex == geometry.mPlaneIndex &&
			bufferedPlaneData.mScrollOffsets == geometry.mScrollOffsets &&
			bufferedPlaneData.mActiveRect == geometry.mActiveRect)
		{
			foundFittingBufferedPlaneDataIndex = i;
			break;
		}
	}

	if (foundFittingBufferedPlaneDataIndex == -1)
	{
		// Find a free index
		for (int i = 0; i < MAX_BUFFER_PLANE_DATA; ++i)
		{
			if (!mBufferedPlaneData[i].mValid)
			{
				foundFittingBufferedPlaneDataIndex = i;
				break;
			}
		}
		RMX_CHECK(foundFittingBufferedPlaneDataIndex != -1, "No free buffered plane data structure found", return);

		BufferedPlaneData& bufferedPlaneData = mBufferedPlaneData[foundFittingBufferedPlaneDataIndex];
		bufferedPlaneData.mPlaneIndex = geometry.mPlaneIndex;
		bufferedPlaneData.mScrollOffsets = geometry.mScrollOffsets;
		bufferedPlaneData.mActiveRect = geometry.mActiveRect;
		bufferedPlaneData.mContent.resize(gameScreenBitmap.getPixelCount());
		bufferedPlaneData.mPrioBlocks.clear();
		bufferedPlaneData.mPrioBlocks.reserve(0x800);
		bufferedPlaneData.mNonPrioBlocks.clear();
		bufferedPlaneData.mNonPrioBlocks.reserve(0x800);

		const uint16* planeData = planeManager.getPlaneDataInVRAM(geometry.mPlaneIndex);
		const uint16 numPatternsPerLine = (geometry.mPlaneIndex <= PlaneManager::PLANE_A) ? planeManager.getPlayfieldSizeInPatterns().x : 64;
		const uint16* scrollOffsetsH = nullptr;
		const uint16* scrollOffsetsV = nullptr;
		uint16 scrollMaskH = 0xff;
		uint16 scrollMaskV = 0;
		bool scrollNoRepeat = false;

		if (geometry.mPlaneIndex == PlaneManager::PLANE_W)
		{
			static uint16 wScrollOffsetX;
			wScrollOffsetX = (uint16)scrollOffsetsManager.getPlaneWScrollOffset().x;
			scrollOffsetsH = &wScrollOffsetX;
			scrollMaskH = 0;
		}
		else
		{
			scrollOffsetsH = scrollOffsetsManager.getScrollOffsetsH(geometry.mScrollOffsets);
			scrollOffsetsV = scrollOffsetsManager.getScrollOffsetsV(geometry.mScrollOffsets);
			scrollMaskV = scrollOffsetsManager.getVerticalScrolling() ? 0x1f : 0;
			scrollNoRepeat = scrollOffsetsManager.getHorizontalScrollNoRepeat(geometry.mScrollOffsets);
		}
		const uint16 positionMaskH = planeManager.getPlayfieldSizeInPixels().x - 1;
		const uint16 positionMaskV = planeManager.getPlayfieldSizeInPixels().y - 1;
		const int16 verticalScrollOffsetBias = scrollOffsetsManager.getVerticalScrollOffsetBias();

		detail::PixelBlockWriter pixelBlockWriter(bufferedPlaneData, mRenderParts.getPatternManager().getPatternCache());

		for (int y = minY; y < maxY; ++y)
		{
			const int position = y * gameScreenBitmap.getWidth();
			pixelBlockWriter.newLine(y, position, (y < paletteManager.mSplitPositionY) ? 0 : 1);

			int vx = minX;
			if (nullptr != scrollOffsetsH)
				vx += (int16)scrollOffsetsH[y & scrollMaskH];

			int startX = minX;
			int endX = maxX;
			if (scrollNoRepeat)
			{
				if (vx > 0x0800)
				{
					startX += 0x1000 - vx;
					vx = 0;
				}
				else if (endX > startX + (positionMaskH - vx))
				{
					endX = startX + (positionMaskH - vx) + 1;
				}
				if (startX >= endX)
					continue;
			}

			if (scrollMaskV == 0)
			{
				// Optimized version of the code below in the else-block
				const int vy = ((nullptr == scrollOffsetsV) ? y : (y + scrollOffsetsV[0])) & positionMaskV;
				const uint16* planeDataForThisLine = &planeData[(vy / 8) * numPatternsPerLine];
				const int patternPixelBaseOffset = (vy & 0x07) * 8;

				// First few pixels until vx gets divisible by 8
				int x = startX;
				{
					vx &= positionMaskH;
					const uint16 patternIndex = planeDataForThisLine[vx / 8];
					const int vxMod8 = vx & 0x07;
					const int pixels = std::min(8 - vxMod8, endX - x);
					pixelBlockWriter.mPatternPixelOffset = patternPixelBaseOffset + vxMod8;
					pixelBlockWriter.addPixels(x, patternIndex, pixels);
					x += pixels;
					vx += pixels;
				}

				// Full blocks of 8 pixels
				pixelBlockWriter.mPatternPixelOffset = patternPixelBaseOffset;
				while (true)
				{
					vx &= positionMaskH;
					const int remainingVx = (positionMaskH - vx + 1);
					const int numPixels = std::min(remainingVx, endX - x) / 8 * 8;
					if (numPixels < 8)
						break;

					const uint16* planeDataPointer = &planeDataForThisLine[vx / 8];
					const int localEndX = x + numPixels;
					while (x < localEndX)
					{
						const uint16 patternIndex = *planeDataPointer;
						pixelBlockWriter.addPixels8(x, patternIndex);
						x += 8;
						++planeDataPointer;
					}
					vx += numPixels;
				}

				// Remaining pixels on the right
				if (x < endX)
				{
					vx &= positionMaskH;
					const uint16 patternIndex = planeDataForThisLine[vx / 8];
					const int pixels = endX - x;
					pixelBlockWriter.addPixels(x, patternIndex, pixels);
				}
			}
			else
			{
				for (int x = startX; x < endX; )
				{
					vx &= positionMaskH;

					int vy;
					if (nullptr == scrollOffsetsV)
					{
						vy = y;
					}
					else
					{
						const int verticalScrollOffset = (scrollMaskV == 0) ? scrollOffsetsV[0] : scrollOffsetsV[((x - verticalScrollOffsetBias) >> 4) & scrollMaskV];
						vy = (y + verticalScrollOffset) & positionMaskV;
					}

					const uint16 patternIndex = planeData[(vx / 8) + (vy / 8) * numPatternsPerLine];
					const int vxMod8 = vx & 0x07;
					const int pixels = std::min(8 - vxMod8, endX - x);

					pixelBlockWriter.mPatternPixelOffset = vxMod8 + (vy & 0x07) * 8;
					pixelBlockWriter.addPixels(x, patternIndex, pixels);
					x += pixels;
					vx += pixels;
				}
			}
		}

		bufferedPlaneData.mValid = true;
	}

	// Write plane data to output
	{
		BufferedPlaneData& bufferedPlaneData = mBufferedPlaneData[foundFittingBufferedPlaneDataIndex];

		const uint32* palettes[2] = { paletteManager.getMainPalette(0).getRawColors(), paletteManager.getMainPalette(1).getRawColors() };

		const std::vector<BufferedPlaneData::PixelBlock>& blocks = geometry.mPriorityFlag ? bufferedPlaneData.mPrioBlocks : bufferedPlaneData.mNonPrioBlocks;
		for (const BufferedPlaneData::PixelBlock& block : blocks)
		{
			const uint8* RESTRICT src = &bufferedPlaneData.mContent[block.mLinearPosition];
			uint32* RESTRICT dstRGBA = &gameScreenBitmap.getData()[block.mLinearPosition];
			const uint32* RESTRICT paletteWithAtex = &palettes[block.mPaletteIndex][block.mAtex];

			// Note: The pixel count is copied into a local variable, as the compiler would otherwise reload it after every written pixel
			const int numPixels = block.mNumPixels;
			if (geometry.mPriorityFlag)
			{
				uint8* RESTRICT dstDepth = &mDepthBuffer[block.mStartCoords.x + block.mStartCoords.y * 0x200];
				detail::writePlanePixels<true>(src, dstRGBA, dstDepth, paletteWithAtex, numPixels);
			}
			else
			{
				detail::writePlanePixels<false>(src, dstRGBA, nullptr, paletteWithAtex, numPixels);
			}
		}

		if (!blocks.empty() && geometry.mPriorityFlag)
			mEmptyDepthBuffer = false;
	}
}

void SoftwareRenderer::renderSprite(const SpriteGeometry& geometry)
{
	Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();

	switch (geometry.mSpriteInfo.getType())
	{
		case RenderItem::Type::VDP_SPRITE:
		{
			const renderitems::VdpSpriteInfo& sprite = static_cast<const renderitems::VdpSpriteInfo&>(geometry.mSpriteInfo);

			const PaletteManager& paletteManager = mRenderParts.getPaletteManager();
			const uint32* palettes[2] = { paletteManager.getMainPalette(0).getRawColors(), paletteManager.getMainPalette(1).getRawColors() };
			const PatternManager::CacheItem* patternCache = mRenderParts.getPatternManager().getPatternCache();

			const uint8 depthValue = (sprite.mPriorityFlag) ? 0x80 : 0;
			const bool useTintColor = (sprite.mTintColor != Color::WHITE || sprite.mAddedColor != Color::TRANSPARENT);

			Recti rect(sprite.mInterpolatedPosition.x, sprite.mInterpolatedPosition.y, sprite.mSize.x * 8, sprite.mSize.y * 8);
			rect = Recti::getIntersection(rect, mCurrentViewport);

			const int minX = rect.x;
			const int maxX = rect.x + rect.width;
			const int minY = rect.y;
			const int maxY = rect.y + rect.height;
			if (rect.width > 0 && rect.height > 0)
			{
				N3DS_PROFILE_COUNT_N(COUNT_SPR_VDP_PIXELS, rect.width * rect.height);
				if (useTintColor)
					N3DS_PROFILE_COUNT(COUNT_SPR_VDP_TINTED);
			}

			// Per row, the sprite is processed in segments of up to 8 pixels that lie in the same pattern (tile): the pattern lookup,
			// flipping and palette offset are the same for all pixels of a segment, so they're done once per segment instead of per pixel.
			//  -> Exactly the same result as doing everything per pixel (pixel positions inside the sprite are never negative, as the
			//     rect starts at the sprite position at the earliest, so "/ 8" and "% 8" are the same as ">> 3" and "& 7").
			const int spriteX = sprite.mInterpolatedPosition.x;
			const int spriteY = sprite.mInterpolatedPosition.y;
			const uint16 firstPattern = sprite.mFirstPattern;
			const bool flipX = (firstPattern & 0x0800) != 0;
			const bool flipY = (firstPattern & 0x1000) != 0;
			const int sizeX = sprite.mSize.x;
			const int sizeY = sprite.mSize.y;

			// Tinted sprites: the tint is the same for the whole sprite, and a pixel's color index can only be 0x00..0x3f (pattern pixels
			// are 0..15, plus the palette offset of up to 0x30), so the tinted colors of both palettes get calculated once per sprite here,
			// with exactly the same calculation as before, instead of again for every single pixel
			uint32 tintedColors[2][0x40];
			if (useTintColor)
			{
				for (int paletteIndex = 0; paletteIndex < 2; ++paletteIndex)
				{
					for (int colorIndex = 0; colorIndex < 0x40; ++colorIndex)
					{
						Color color = Color::fromABGR32(palettes[paletteIndex][colorIndex]);
						color.r = saturate(sprite.mAddedColor.r + color.r * sprite.mTintColor.r);
						color.g = saturate(sprite.mAddedColor.g + color.g * sprite.mTintColor.g);
						color.b = saturate(sprite.mAddedColor.b + color.b * sprite.mTintColor.b);
						color.a = saturate(sprite.mAddedColor.a + color.a * sprite.mTintColor.a);
						tintedColors[paletteIndex][colorIndex] = color.getABGR32();
					}
				}
			}

			for (int y = minY; y < maxY; ++y)
			{
				const int paletteIndex = (y < paletteManager.mSplitPositionY) ? 0 : 1;
				const uint32* palette = palettes[paletteIndex];
				const uint32* tintedPalette = tintedColors[paletteIndex];
				const uint8* depthRow = &mDepthBuffer[y * 0x200];
				uint32* dstRow = gameScreenBitmap.getPixelPointer(0, y);

				const int vy = y - spriteY;
				int patternY = vy >> 3;
				if (flipY)
					patternY = sizeY - patternY - 1;
				const int pixelRowOffset = (vy & 7) * 8;

				int x = minX;
				while (x < maxX)
				{
					const int segmentStart = x;
					const int vx = x - spriteX;
					const int pixelColumn = vx & 7;
					const int segmentEnd = std::min(x + (8 - pixelColumn), maxX);

					int patternX = vx >> 3;
					if (flipX)
						patternX = sizeX - patternX - 1;

					const uint16 patternIndex = firstPattern + patternY + patternX * sizeY;
					const PatternManager::CacheItem::Pattern& pattern = patternCache[patternIndex & 0x07ff].mFlipVariation[(patternIndex >> 11) & 3];
					const uint8* patternPixels = &pattern.mPixels[pixelRowOffset + pixelColumn];
					const uint8 atex = (uint8)((patternIndex >> 9) & 0x30);

					for (; x < segmentEnd; ++x)
					{
						// Depth test
						if (depthValue < depthRow[x])
							continue;

						const uint8 colorIndex = (uint8)(patternPixels[x - segmentStart] + atex);
						if (colorIndex & 0x0f)
						{
							uint32& dst = dstRow[x];
							if (useTintColor)
							{
								const uint32 src = tintedPalette[colorIndex];
								BlitterHelper::blendPixelAlpha((uint8*)&dst, (const uint8*)&src);
							}
							else
							{
								dst = palette[colorIndex];
							}
						}
					}
				}
			}
			break;
		}

		case RenderItem::Type::PALETTE_SPRITE:
		case RenderItem::Type::COMPONENT_SPRITE:
		{
			// Shared code for palette & component sprite rendering
			const renderitems::CustomSpriteInfoBase& spriteBase = static_cast<const renderitems::CustomSpriteInfoBase&>(geometry.mSpriteInfo);
			if (nullptr == spriteBase.mCacheItem)
				break;

			const bool isPaletteSprite = (geometry.mSpriteInfo.getType() == RenderItem::Type::PALETTE_SPRITE);

			const PaletteManager& paletteManager = mRenderParts.getPaletteManager();
			BitmapViewMutable<uint8> depthBufferView(mDepthBuffer, Vec2i(0x200, 0x100));	// Depth buffer uses a fixed size...

			// Build blitter options
			Blitter::Options blitterOptions;
			Vec4f tintColor = spriteBase.mTintColor;		// Using Vec4f instead of Color to prevent clamp into [0.0f, 1.0f]
			Vec4f addedColor = spriteBase.mAddedColor;
			{
				if (spriteBase.mUseGlobalComponentTint)
				{
					paletteManager.applyGlobalComponentTint(tintColor, addedColor);
				}

				const bool hasTransform = !spriteBase.mTransformation.isIdentity();
				if (hasTransform && isPaletteSprite)
					N3DS_PROFILE_COUNT(COUNT_SPR_PALETTE_TRANSFORMED);
				blitterOptions.mTransform = hasTransform ? *spriteBase.mTransformation.mMatrix : nullptr;
				blitterOptions.mInvTransform = hasTransform ? *spriteBase.mTransformation.mInverse : nullptr;
				blitterOptions.mSamplingMode = SamplingMode::POINT;
				blitterOptions.mBlendMode = spriteBase.mBlendMode;
				blitterOptions.mTintColor = (tintColor != Color::WHITE) ? &tintColor : nullptr;
				blitterOptions.mAddedColor = (addedColor != Color::TRANSPARENT) ? &addedColor : nullptr;
				blitterOptions.mDepthBuffer = (mEmptyDepthBuffer && !spriteBase.mPriorityFlag) ? nullptr : &depthBufferView;
				blitterOptions.mDepthTestValue = (spriteBase.mPriorityFlag) ? 0x80 : 0;
			}

			if (isPaletteSprite)
			{
				// Palette sprite specific code
				const renderitems::PaletteSpriteInfo& spriteInfo = static_cast<const renderitems::PaletteSpriteInfo&>(spriteBase);

				const PaletteSprite& paletteSprite = *static_cast<PaletteSprite*>(spriteInfo.mCacheItem->mSprite);
				const PaletteBitmap& paletteBitmap = spriteInfo.mUseUpscaledSprite ? paletteSprite.getUpscaledBitmap() : paletteSprite.getBitmap();
				const Blitter::IndexedSpriteWrapper spriteWrapper(paletteBitmap.getData(), paletteBitmap.getSize(), -paletteSprite.mOffset);

				const PaletteBase& primaryPalette = (nullptr == spriteInfo.mPrimaryPalette) ? paletteManager.getMainPalette(0) : *spriteInfo.mPrimaryPalette;
				const Blitter::PaletteWrapper paletteWrapper(primaryPalette.getRawColors() + spriteInfo.mAtex, primaryPalette.getSize() - spriteInfo.mAtex);

				// Handle screen palette split
				const int splitY = paletteManager.mSplitPositionY;
				if (splitY < mGameResolution.y)
				{
					const PaletteBase& secondaryPalette = (nullptr == spriteInfo.mSecondaryPalette) ? paletteManager.getMainPalette(1) : *spriteInfo.mSecondaryPalette;
					const Blitter::PaletteWrapper paletteWrapper2(secondaryPalette.getRawColors() + spriteInfo.mAtex, secondaryPalette.getSize() - spriteInfo.mAtex);

					Recti targetRect = Recti::getIntersection(mCurrentViewport, Recti(0, 0, mGameResolution.x, splitY));
					mBlitter.blitIndexed(Blitter::OutputWrapper(gameScreenBitmap, targetRect), spriteWrapper, paletteWrapper, spriteInfo.mInterpolatedPosition, blitterOptions);

					targetRect = Recti::getIntersection(mCurrentViewport, Recti(0, splitY, mGameResolution.x, mGameResolution.y - splitY));
					mBlitter.blitIndexed(Blitter::OutputWrapper(gameScreenBitmap, targetRect), spriteWrapper, paletteWrapper2, spriteInfo.mInterpolatedPosition, blitterOptions);
				}
				else
				{
					mBlitter.blitIndexed(Blitter::OutputWrapper(gameScreenBitmap, mCurrentViewport), spriteWrapper, paletteWrapper, spriteInfo.mInterpolatedPosition, blitterOptions);
				}
			}
			else
			{
				// Component sprite specific code
				const renderitems::ComponentSpriteInfo& spriteInfo = static_cast<const renderitems::ComponentSpriteInfo&>(spriteBase);

				const ComponentSprite& componentSprite = *static_cast<ComponentSprite*>(spriteInfo.mCacheItem->mSprite);
				const Blitter::SpriteWrapper spriteWrapper(componentSprite.getBitmap(), -componentSprite.mOffset);

				mBlitter.blitSprite(Blitter::OutputWrapper(gameScreenBitmap, mCurrentViewport), spriteWrapper, spriteInfo.mInterpolatedPosition, blitterOptions);
			}

			if (spriteBase.mPriorityFlag)
				mEmptyDepthBuffer = false;
			break;
		}

		case RenderItem::Type::SPRITE_MASK:
		{
			// Overwrite sprites with plane rendering results in given rect
			const renderitems::SpriteMaskInfo& mask = static_cast<const renderitems::SpriteMaskInfo&>(geometry.mSpriteInfo);
			if (mask.mSize.x > 0 && mask.mSize.y > 0)
			{
				const int minX = clamp(mask.mInterpolatedPosition.x, 0, gameScreenBitmap.getWidth());
				const int maxX = clamp(mask.mInterpolatedPosition.x + mask.mSize.x, 0, gameScreenBitmap.getWidth());
				const int bytes = (maxX - minX) * 4;
				if (bytes > 0)
				{
					const int minY = clamp(mask.mInterpolatedPosition.y, 0, gameScreenBitmap.getHeight());
					const int maxY = clamp(mask.mInterpolatedPosition.y + mask.mSize.y, 0, gameScreenBitmap.getHeight());

					for (int line = minY; line < maxY; ++line)
					{
						const uint32 offset = minX + line * gameScreenBitmap.getWidth();
						memcpy(&gameScreenBitmap[offset], &mGameScreenCopy[offset], bytes);
					}
				}
			}
			break;
		}

		case RenderItem::Type::RECTANGLE:
		case RenderItem::Type::TEXT:
		case RenderItem::Type::VIEWPORT:
		case RenderItem::Type::INVALID:
			break;
	}
}
