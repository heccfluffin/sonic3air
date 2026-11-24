/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: draws bosses and levels straight from the player's ROM, for the journal's postcards.
*	Nothing of the original game is included: the art is decompressed from the ROM the player provides, like the game does.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"

#if defined(PLATFORM_3DS)

#include "sonic3air/menu/N3DSRomSprites.h"

#include "oxygen/simulation/EmulatorInterface.h"

#include <algorithm>
#include <map>
#include <vector>


namespace n3ds
{
	namespace romsprites
	{
		namespace
		{
			const uint32 MAIN_PALETTE = 0x0a8a3c;				// Palette line 0 as the game loads it (characters and shared colors)
			const uint32 PATTERN_LOADING_TABLE = 0x09238c;
			const uint32 LEVEL_DATA_TABLE = 0x091f0c;			// Per act: level art, blocks and chunks (two of each), see MainGame.getLevelDataPointer
			const uint32 LEVEL_LAYOUT_TABLE = 0x09d5c0;
			const uint32 PALETTE_TABLE = 0x0a872c;				// See copyPaletteDataFromTable

			struct Rom
			{
				const uint8* mData = nullptr;
				uint32 mSize = 0;

				inline bool valid(uint32 address, uint32 bytes) const	{ return nullptr != mData && address + bytes <= mSize; }
				inline uint8 read8(uint32 address) const				{ return (address < mSize) ? mData[address] : 0; }
				inline uint16 read16(uint32 address) const				{ return (uint16)((read8(address) << 8) | read8(address + 1)); }
				inline uint32 read32(uint32 address) const				{ return ((uint32)read16(address) << 16) | read16(address + 2); }
			};

			bool getRom(Rom& rom)
			{
				EmulatorInterface& emulatorInterface = EmulatorInterface::instance();
				rom.mData = emulatorInterface.getRom();
				rom.mSize = emulatorInterface.getRomSize();
				return rom.valid(0x3ffff0, 0x10);
			}

			struct Vram
			{
				std::vector<uint8> mData = std::vector<uint8>(0x10000, 0);

				void put(uint32 target, const uint8* data, size_t size)
				{
					for (size_t i = 0; i < size && target + i < mData.size(); ++i)
						mData[target + i] = data[i];
				}
				void put(uint32 target, const std::vector<uint8>& data)  { put(target, data.data(), data.size()); }

				inline uint8 pixel(uint32 tile, int x, int y) const
				{
					const uint8 byte = mData[(tile * 32 + y * 4 + x / 2) & 0xffff];
					return (x & 1) ? (byte & 0x0f) : (byte >> 4);
				}
			};

			// Kosinski: a descriptor bit field, then literal bytes and copies from what was already written; returns false on bad data
			bool decompressKosinski(const Rom& rom, uint32& position, std::vector<uint8>& output, size_t maxSize = 0x10000)
			{
				uint16 descriptor = 0;
				int remaining = 0;
				const auto readByte = [&]() { return rom.read8(position++); };
				const auto readDescriptor = [&]() { descriptor = (uint16)(readByte() | (readByte() << 8)); remaining = 15; };
				const auto readBit = [&]()
				{
					const uint8 bit = descriptor & 1;
					descriptor >>= 1;
					if (--remaining < 0)
						readDescriptor();
					return bit;
				};
				readDescriptor();
				while (rom.valid(position, 1))
				{
					while (readBit() == 1)
					{
						output.push_back(readByte());
						if (output.size() > maxSize)
							return false;
					}
					int length = 0;
					int offset = 0;
					if (readBit() == 0)
					{
						const uint8 high = readBit();
						const uint8 low = readBit();
						length = high * 2 + low + 2;
						offset = (int)readByte() - 0x100;
					}
					else
					{
						const uint8 low = readByte();
						const uint8 high = readByte();
						offset = ((high & 0xf8) << 5) + low - 0x2000;
						if ((high & 7) == 0)
						{
							const uint8 value = readByte();
							if (value == 0)
								return true;
							if (value == 1)
								continue;
							length = value + 1;
						}
						else
						{
							length = (high & 7) + 2;
						}
					}
					for (int i = 0; i < length; ++i)
					{
						const int source = (int)output.size() + offset;
						output.push_back((source >= 0) ? output[source] : 0);
					}
					if (output.size() > maxSize)
						return false;
				}
				return false;
			}

			// Kosinski moduled: the size, then blocks of up to 0x1000 bytes, each padded to 16 bytes counted from the start of the data
			bool decompressKosinskiModuled(const Rom& rom, uint32 address, std::vector<uint8>& output)
			{
				if (!rom.valid(address, 2))
					return false;
				uint16 size = rom.read16(address);
				if (size == 0xa000)
					size = 0x8000;
				const uint32 start = address + 2;
				uint32 position = start;
				while (size > 0)
				{
					std::vector<uint8> module;
					if (!decompressKosinski(rom, position, module, 0x2000))
						return false;
					const uint16 bytes = std::min<uint16>(size, 0x1000);
					module.resize(bytes, 0);
					output.insert(output.end(), module.begin(), module.end());
					if (size <= 0x1000)
						break;
					size -= bytes;
					position = start + ((position - start + 15) & ~15u);
				}
				return true;
			}

			// Nemesis: a code table for runs of nybbles, then the bit stream; rows optionally XOR'ed with the previous row
			bool decompressNemesis(const Rom& rom, uint32 address, std::vector<uint8>& output)
			{
				if (!rom.valid(address, 4))
					return false;
				const uint16 header = rom.read16(address);
				const bool xorMode = (header & 0x8000) != 0;
				const uint32 numTiles = header & 0x7fff;
				uint32 position = address + 2;

				struct Code { uint8 mNybble; uint8 mCount; };
				std::map<uint32, Code> codes;		// Key: length << 8 | code
				uint8 nybble = 0;
				uint8 value = rom.read8(position++);
				while (value != 0xff)
				{
					if (value & 0x80)
					{
						nybble = value & 0x0f;
						value = rom.read8(position++);
					}
					const uint8 count = ((value >> 4) & 7) + 1;
					const uint8 length = value & 0x0f;
					const uint8 code = rom.read8(position++);
					codes[((uint32)length << 8) | code] = Code { nybble, count };
					value = rom.read8(position++);
					if (!rom.valid(position, 1))
						return false;
				}

				uint32 bitPosition = position * 8;
				const auto readBit = [&]()  { const uint8 bit = (rom.read8(bitPosition >> 3) >> (7 - (bitPosition & 7))) & 1; ++bitPosition; return bit; };
				const auto readBits = [&](int n)  { uint32 result = 0; for (int i = 0; i < n; ++i) result = (result << 1) | readBit(); return result; };

				const uint32 total = numTiles * 64;
				std::vector<uint8> nybbles;
				nybbles.reserve(total);
				while (nybbles.size() < total)
				{
					uint32 code = 0;
					int length = 0;
					uint8 runValue = 0;
					uint8 runCount = 0;
					while (true)
					{
						code = (code << 1) | readBit();
						++length;
						if (length == 6 && code == 0x3f)
						{
							runCount = (uint8)readBits(3) + 1;
							runValue = (uint8)readBits(4);
							break;
						}
						const auto it = codes.find(((uint32)length << 8) | code);
						if (it != codes.end())
						{
							runValue = it->second.mNybble;
							runCount = it->second.mCount;
							break;
						}
						if (length > 13 || !rom.valid(bitPosition >> 3, 1))
							return false;
					}
					for (uint8 i = 0; i < runCount && nybbles.size() < total; ++i)
						nybbles.push_back(runValue);
				}

				uint32 previous = 0;
				for (uint32 r = 0; r < total; r += 8)
				{
					uint32 row = 0;
					for (int i = 0; i < 8; ++i)
						row = (row << 4) | nybbles[r + i];
					if (xorMode)
					{
						row ^= previous;
						previous = row;
					}
					output.push_back((uint8)(row >> 24));
					output.push_back((uint8)(row >> 16));
					output.push_back((uint8)(row >> 8));
					output.push_back((uint8)row);
				}
				return true;
			}

			bool load(const Rom& rom, const ArtLoad& artLoad, Vram& vram)
			{
				std::vector<uint8> data;
				switch (artLoad.mType)
				{
					case ArtLoad::Type::NONE:
						return true;

					case ArtLoad::Type::KOSINSKI_MODULED:
						if (!decompressKosinskiModuled(rom, artLoad.mAddress, data))
							return false;
						vram.put(artLoad.mTarget, data);
						return true;

					case ArtLoad::Type::NEMESIS:
						if (!decompressNemesis(rom, artLoad.mAddress, data))
							return false;
						vram.put(artLoad.mTarget, data);
						return true;

					case ArtLoad::Type::PATTERN_LOADING_CUE:
					{
						// A list of Nemesis art and where it goes in VRAM
						uint32 address = PATTERN_LOADING_TABLE + rom.read16(PATTERN_LOADING_TABLE + artLoad.mAddress * 2);
						const int count = rom.read16(address) + 1;
						address += 2;
						for (int i = 0; i < count; ++i, address += 6)
						{
							data.clear();
							if (!decompressNemesis(rom, rom.read32(address), data))
								return false;
							vram.put(rom.read16(address + 4), data);
						}
						return true;
					}

					case ArtLoad::Type::CHARACTER_FRAME:
					{
						// The players' art isn't compressed: for each frame, a list of tile runs to copy (character_graphics.lemon)
						uint32 address = artLoad.mAddress + rom.read16(artLoad.mAddress + artLoad.mTarget * 2);
						const int count = std::min<int>(rom.read16(address), 16);
						uint16 target = 0;
						for (int i = 0; i < count; ++i)
						{
							address += 2;
							const uint16 entry = rom.read16(address);
							const uint32 source = artLoad.mSource + ((entry & 0x0fff) << 5);
							const int bytes = (0x10 + ((entry & 0xf000) >> 8)) * 2;
							data.resize(bytes);
							for (int k = 0; k < bytes; ++k)
								data[k] = rom.read8(source + k);
							vram.put(target, data);
							target += bytes;
						}
						return true;
					}
				}
				return false;
			}

			void readPaletteLine(const Rom& rom, uint32 address, uint32* outColors)
			{
				for (int i = 0; i < 16; ++i)
				{
					const uint16 color = rom.read16(address + i * 2);
					const uint32 r = ((color >> 1) & 7) * 36;
					const uint32 g = ((color >> 5) & 7) * 36;
					const uint32 b = ((color >> 9) & 7) * 36;
					outColors[i] = 0xff000000 | (b << 16) | (g << 8) | r;		// ABGR, like the bitmaps
				}
			}

			// Palette data from the game's table, into the lines it targets (lines 1..3 for levels)
			void loadPaletteEntry(const Rom& rom, uint8 index, uint32 palettes[4][16])
			{
				const uint32 entry = PALETTE_TABLE + index * 8;
				const uint32 source = rom.read32(entry);
				const int line = (rom.read16(entry + 4) - 0xfc00) / 0x20;
				const int lines = (rom.read16(entry + 6) + 1) * 2 / 16;
				for (int i = 0; i < lines; ++i)
				{
					if (line + i >= 0 && line + i < 4)
						readPaletteLine(rom, source + i * 32, palettes[line + i]);
				}
			}

			// S3K mappings: a frame's offset, then a count and 6 bytes per piece (y, size, tile word, x)
			struct Piece { int x, y, width, height; uint16 word; };

			bool readPieces(const Rom& rom, uint32 mapping, uint16 attributes, uint8 frame, std::vector<Piece>& outPieces)
			{
				if (!rom.valid(mapping + frame * 2, 2))
					return false;
				const uint32 frameAddress = mapping + rom.read16(mapping + frame * 2);
				const int count = rom.read16(frameAddress);
				if (count <= 0 || count > 64 || !rom.valid(frameAddress + 2, count * 6))
					return false;
				for (int i = 0; i < count; ++i)
				{
					const uint32 address = frameAddress + 2 + i * 6;
					Piece piece;
					piece.y = (int8)rom.read8(address);
					const uint8 size = rom.read8(address + 1);
					piece.width = ((size >> 2) & 3) + 1;
					piece.height = (size & 3) + 1;
					piece.word = (uint16)(rom.read16(address + 2) + attributes);
					piece.x = (int16)rom.read16(address + 4);
					outPieces.push_back(piece);
				}
				return true;
			}

			// One 8x8 tile of a tile word (with its flips and palette line) onto the bitmap; color 0 is see-through
			void drawTile(Bitmap& bitmap, const Vram& vram, const uint32 palettes[4][16], uint16 word, int x0, int y0)
			{
				const uint32 tile = word & 0x7ff;
				const uint32* palette = palettes[(word >> 13) & 3];
				const bool flipX = (word & 0x800) != 0;
				const bool flipY = (word & 0x1000) != 0;
				for (int py = 0; py < 8; ++py)
				{
					const int y = y0 + py;
					if (y < 0 || y >= bitmap.getHeight())
						continue;
					for (int px = 0; px < 8; ++px)
					{
						const int x = x0 + px;
						if (x < 0 || x >= bitmap.getWidth())
							continue;
						const uint8 color = vram.pixel(tile, flipX ? (7 - px) : px, flipY ? (7 - py) : py);
						if (color != 0)
							*bitmap.getPixelPointer(x, y) = palette[color];
					}
				}
			}
		}

		bool draw(const RomSprite& sprite, bool flipX, Bitmap& outBitmap, Vec2i* outOrigin)
		{
			Rom rom;
			if (!getRom(rom))
				return false;

			Vram vram;
			for (const ArtLoad& artLoad : sprite.mLoads)
			{
				if (!load(rom, artLoad, vram))
					return false;
			}

			uint32 palettes[4][16];
			readPaletteLine(rom, MAIN_PALETTE, palettes[0]);
			if (sprite.mSuperColors != 0)
			{
				uint32 superColors[16];
				readPaletteLine(rom, sprite.mSuperColors, superColors);
				for (int i = 0; i < 3; ++i)
					palettes[0][2 + i] = superColors[i];
			}
			readPaletteLine(rom, sprite.mPaletteLine1, palettes[1]);
			for (int line = 2; line < 4; ++line)
				for (int i = 0; i < 16; ++i)
					palettes[line][i] = 0xff000000 | (i * 0x101010);

			// All pieces of all parts, placed (and flipped) like the game does
			struct Placed { Piece piece; uint16 priority; int order; };
			std::vector<Placed> placed;
			int x0 = 0x7fff, y0 = 0x7fff, x1 = -0x7fff, y1 = -0x7fff;
			int order = 0;
			for (const SpritePart& part : sprite.mParts)
			{
				if (part.mMapping == 0)
					break;
				std::vector<Piece> pieces;
				if (!readPieces(rom, part.mMapping, part.mAttributes, part.mFrame, pieces))
					return false;
				for (Piece piece : pieces)
				{
					piece.x += part.mX;
					piece.y += part.mY;
					if (flipX)
					{
						piece.x = -piece.x - piece.width * 8;
						piece.word ^= 0x800;
					}
					placed.push_back(Placed { piece, part.mPriority, order++ });
					x0 = std::min(x0, piece.x);
					y0 = std::min(y0, piece.y);
					x1 = std::max(x1, piece.x + piece.width * 8);
					y1 = std::max(y1, piece.y + piece.height * 8);
				}
			}
			if (placed.empty() || x1 - x0 > 320 || y1 - y0 > 240)
				return false;

			// Back to front: higher priority values first; then the later pieces (the first piece of the first part is in front)
			std::stable_sort(placed.begin(), placed.end(), [](const Placed& a, const Placed& b)
				{ return (a.priority != b.priority) ? (a.priority > b.priority) : (a.order > b.order); });

			outBitmap.create(x1 - x0, y1 - y0, 0);
			if (nullptr != outOrigin)
				*outOrigin = Vec2i(-x0, -y0);
			for (const Placed& entry : placed)
			{
				const Piece& piece = entry.piece;
				const bool flippedX = (piece.word & 0x800) != 0;
				const bool flippedY = (piece.word & 0x1000) != 0;
				for (int tx = 0; tx < piece.width; ++tx)
				{
					for (int ty = 0; ty < piece.height; ++ty)
					{
						// Tiles go down the columns first; a flipped piece also has its tiles in mirrored order
						const int column = flippedX ? (piece.width - 1 - tx) : tx;
						const int row = flippedY ? (piece.height - 1 - ty) : ty;
						const uint16 word = (uint16)((piece.word & 0xf800) | ((piece.word + column * piece.height + row) & 0x7ff));
						drawTile(outBitmap, vram, palettes, word, piece.x - x0 + tx * 8, piece.y - y0 + ty * 8);
					}
				}
			}
			return true;
		}

		namespace
		{
			// Animated level tiles: the zone's list of animated patterns (all loaded with their first frame), per zone and act
			const uint32 ANIMATED_PATTERNS[14][2] =
			{
				{ 0x028750, 0x02879c }, { 0x0287f4, 0x02882c }, { 0x028862, 0x028862 }, { 0x028882, 0x028882 },
				{ 0x028906, 0x028948 }, { 0x028990, 0x028990 }, { 0x0289a2, 0x0289cc }, { 0x0289e8, 0x0289e8 },
				{ 0x028a6a, 0x028a6a }, { 0x028a6a, 0x028a84 }, { 0x028aa4, 0 }, { 0x028aee, 0x028aee }, { 0, 0 }, { 0, 0 }
			};

			struct Level
			{
				Vram mVram;
				std::vector<uint8> mBlocks;
				std::vector<uint8> mChunks;
				std::vector<uint8> mLayout;
				uint32 mPalettes[4][16];

				inline uint16 layout16(uint32 offset) const  { return (offset + 1 < mLayout.size()) ? (uint16)((mLayout[offset] << 8) | mLayout[offset + 1]) : 0; }

				uint8 chunkAt(int cx, int cy, bool background) const
				{
					const int width = layout16(background ? 2 : 0);
					const int height = layout16(background ? 6 : 4);
					if (cx < 0 || cy < 0 || cx >= width || cy >= height)
						return 0;
					const uint32 row = (uint32)layout16(8 + cy * 4 + (background ? 2 : 0)) - 0x8000;
					return (row + cx < mLayout.size()) ? mLayout[row + cx] : 0;
				}

				uint16 tileWordAt(int x, int y, bool background) const
				{
					const uint8 chunk = chunkAt(x >> 7, y >> 7, background);
					const uint32 chunkOffset = chunk * 128 + ((y & 127) >> 4) * 16 + ((x & 127) >> 4) * 2;
					if (chunkOffset + 1 >= mChunks.size())
						return 0;
					const uint16 blockWord = (uint16)((mChunks[chunkOffset] << 8) | mChunks[chunkOffset + 1]);
					int bx = (x & 15) >> 3;
					int by = (y & 15) >> 3;
					if (blockWord & 0x400)	bx = 1 - bx;
					if (blockWord & 0x800)	by = 1 - by;
					const uint32 blockOffset = (blockWord & 0x3ff) * 8 + (by * 2 + bx) * 2;
					if (blockOffset + 1 >= mBlocks.size())
						return 0;
					uint16 word = (uint16)((mBlocks[blockOffset] << 8) | mBlocks[blockOffset + 1]);
					if (blockWord & 0x400)	word ^= 0x800;
					if (blockWord & 0x800)	word ^= 0x1000;
					return word;
				}
			};

			bool loadLevel(const Rom& rom, const LevelView& view, Level& level)
			{
				if (view.mZone >= 14 || view.mAct >= 2)
					return false;
				const uint32 header = LEVEL_DATA_TABLE + (view.mZone * 16 + view.mAct * 8) * 3;
				const uint32 art1 = rom.read32(header) & 0xffffff;
				const uint32 art2 = rom.read32(header + 4) & 0xffffff;
				const uint32 blocks1 = rom.read32(header + 8) & 0xffffff;
				const uint32 blocks2 = rom.read32(header + 12) & 0xffffff;
				const uint32 chunks1 = rom.read32(header + 16) & 0xffffff;
				const uint32 chunks2 = rom.read32(header + 20) & 0xffffff;

				// 8x8 level tiles (the second set right after the first), 16x16 blocks and 128x128 chunks
				std::vector<uint8> art;
				if (!decompressKosinskiModuled(rom, art1, art))
					return false;
				level.mVram.put(0, art);
				if (art2 != art1)
				{
					art.clear();
					if (!decompressKosinskiModuled(rom, art2, art))
						return false;
					level.mVram.put(rom.read16(art1), art);
				}
				uint32 position = blocks1;
				if (!decompressKosinski(rom, position, level.mBlocks))
					return false;
				if (blocks2 != blocks1)
				{
					position = blocks2;
					if (!decompressKosinski(rom, position, level.mBlocks))
						return false;
				}
				position = chunks1;
				if (!decompressKosinski(rom, position, level.mChunks))
					return false;
				if (chunks2 != chunks1)
				{
					position = chunks2;
					if (!decompressKosinski(rom, position, level.mChunks))
						return false;
				}
				const uint32 layout = rom.read32(LEVEL_LAYOUT_TABLE + view.mZone * 8 + view.mAct * 4);
				if (!rom.valid(layout, 0x1000))
					return false;
				level.mLayout.assign(rom.mData + layout, rom.mData + layout + 0x1000);

				// Palette: line 0 for the characters, the level's lines from its palette entry
				for (int line = 0; line < 4; ++line)
					for (int i = 0; i < 16; ++i)
						level.mPalettes[line][i] = 0xff000000;
				readPaletteLine(rom, MAIN_PALETTE, level.mPalettes[0]);
				loadPaletteEntry(rom, rom.read8(header + 8), level.mPalettes);

				// Animated tiles with their first frame (duration, art, VRAM target, frames, tiles, then the frame list)
				const uint32 animated = ANIMATED_PATTERNS[view.mZone][view.mAct];
				if (0 != animated)
				{
					const int count = rom.read16(animated) + 1;
					uint32 entry = animated + 2;
					for (int i = 0; i < count && rom.valid(entry, 9); ++i)
					{
						const uint32 source = (rom.read32(entry) & 0xffffff) + rom.read8(entry + 8) * 32;
						const uint32 bytes = rom.read8(entry + 7) * 32;
						if (rom.valid(source, bytes))
							level.mVram.put(rom.read16(entry + 4), rom.mData + source, bytes);
						const int frames = rom.read8(entry + 6) * ((rom.read8(entry) & 0x80) ? 2 : 1);
						entry += 8 + ((frames + 1) & ~1);
					}
				}

				// Background art some zones generate on the fly for their parallax (here with no shift): two copies per strip
				if (view.mZone == 7)
				{
					const uint32 STRIPS[2][3] = { { 0x0ba1c0, 0x3700, 0x028114 }, { 0x0ba9c0, 0x3aa0, 0x028198 } };
					for (const auto& strip : STRIPS)
					{
						const uint32 bytes1 = rom.read16(strip[2]) * 2;
						const uint32 bytes2 = rom.read16(strip[2] + 2) * 2;
						if (rom.valid(strip[0], std::max(bytes1, bytes2)))
						{
							level.mVram.put(strip[1], rom.mData + strip[0], bytes1);
							level.mVram.put(strip[1] + bytes1, rom.mData + strip[0], bytes2);
						}
					}
				}

				// What Angel Island 2 loads before Sonic's boss (level_aiz.lemon): other blocks, more art, the bombing scene palette
				if (view.mSetup == LevelView::Setup::AIZ2_BOSS)
				{
					std::vector<uint8> blocks;
					position = 0x3b1372;
					if (!decompressKosinski(rom, position, blocks))
						return false;
					if (level.mBlocks.size() < 0xab8 + blocks.size())
						level.mBlocks.resize(0xab8 + blocks.size(), 0);
					std::copy(blocks.begin(), blocks.end(), level.mBlocks.begin() + 0xab8);
					art.clear();
					if (!decompressKosinskiModuled(rom, 0x3b48c6, art))
						return false;
					level.mVram.put(0x3f80, art);
					art.clear();
					if (!decompressKosinskiModuled(rom, 0x399cc4, art))
						return false;
					level.mVram.put(0xa000, art);
					loadPaletteEntry(rom, 0x30, level.mPalettes);
				}
				return true;
			}

			void drawLayer(const Level& level, bool background, int viewX, int viewY, Bitmap& bitmap)
			{
				for (int ty = viewY >> 3; ty <= (viewY + bitmap.getHeight() - 1) >> 3; ++ty)
				{
					for (int tx = viewX >> 3; tx <= (viewX + bitmap.getWidth() - 1) >> 3; ++tx)
					{
						const uint16 word = level.tileWordAt(tx * 8, ty * 8, background);
						drawTile(bitmap, level.mVram, level.mPalettes, word, tx * 8 - viewX, ty * 8 - viewY);
					}
				}
			}
		}

		bool drawLevel(const LevelView& view, int width, int height, Bitmap& outBitmap)
		{
			Rom rom;
			if (!getRom(rom))
				return false;
			Level level;
			if (!loadLevel(rom, view, level))
				return false;

			outBitmap.create(width, height, 0xff000000);
			drawLayer(level, true, view.mBackgroundX, view.mBackgroundY, outBitmap);
			if (view.mHasForeground)
				drawLayer(level, false, view.mForegroundX, view.mForegroundY, outBitmap);
			return true;
		}
	}
}

#endif
