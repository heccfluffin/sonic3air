/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: draws boss sprites straight from the player's ROM, for the journal's postcards.
*	Nothing of the original game is included: the art is decompressed from the ROM the player provides, like the game does.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"

#if defined(PLATFORM_3DS)

#include "sonic3air/menu/N3DSRomSprites.h"

#include "oxygen/rendering/utils/Kosinski.h"
#include "oxygen/simulation/EmulatorInterface.h"

#include <map>
#include <vector>


namespace n3ds
{
	namespace romsprites
	{
		namespace
		{
			const uint32 CHARACTER_PALETTE = 0x060b6a;		// Palette line 0: Sonic's colors, also used by bosses
			const uint32 PATTERN_LOADING_TABLE = 0x09238c;

			struct Rom
			{
				const uint8* mData = nullptr;
				uint32 mSize = 0;

				inline bool valid(uint32 address, uint32 bytes) const	{ return nullptr != mData && address + bytes <= mSize; }
				inline uint8 read8(uint32 address) const				{ return (address < mSize) ? mData[address] : 0; }
				inline uint16 read16(uint32 address) const				{ return (uint16)((read8(address) << 8) | read8(address + 1)); }
				inline uint32 read32(uint32 address) const				{ return ((uint32)read16(address) << 16) | read16(address + 2); }
			};

			struct Vram
			{
				std::vector<uint8> mData = std::vector<uint8>(0x10000, 0);

				void put(uint32 target, const std::vector<uint8>& data)
				{
					for (size_t i = 0; i < data.size() && target + i < mData.size(); ++i)
						mData[target + i] = data[i];
				}
			};

			// Kosinski moduled: like the engine's own sprite decoding (SpriteCollection, encoding KOSINSKI)
			bool decompressKosinskiModuled(const Rom& rom, uint32 address, std::vector<uint8>& output)
			{
				if (!rom.valid(address, 2))
					return false;
				EmulatorInterface& emulatorInterface = EmulatorInterface::instance();
				uint16 size = rom.read16(address);
				if (size == 0xa000)
					size = 0x8000;
				uint32 inputAddress = address + 2;
				static uint8 buffer[0x2000];
				while (size > 0)
				{
					if (!rom.valid(inputAddress, 0x10))
						return false;
					uint8* pointer = buffer;
					Kosinski::decompress(emulatorInterface, pointer, inputAddress);
					const uint16 bytes = std::min<uint16>(size, 0x1000);
					output.insert(output.end(), buffer, buffer + bytes);
					if (size < 0x1000)
						break;
					size -= bytes;
					inputAddress += 8;		// As the engine does it
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
		}

		bool draw(const RomSprite& sprite, Bitmap& outBitmap)
		{
			EmulatorInterface& emulatorInterface = EmulatorInterface::instance();
			Rom rom;
			rom.mData = emulatorInterface.getRom();
			rom.mSize = emulatorInterface.getRomSize();
			if (!rom.valid(0x3ffff0, 0x10) || !rom.valid(sprite.mAttributeTable, 6))
				return false;

			Vram vram;
			for (const ArtLoad& artLoad : sprite.mLoads)
			{
				if (!load(rom, artLoad, vram))
					return false;
			}

			uint32 palettes[4][16];
			readPaletteLine(rom, CHARACTER_PALETTE, palettes[0]);
			readPaletteLine(rom, sprite.mPaletteLine1, palettes[1]);
			for (int line = 2; line < 4; ++line)
				for (int i = 0; i < 16; ++i)
					palettes[line][i] = 0xff000000 | (i * 0x101010);

			// S3K mappings: a frame's offset, then a count and 6 bytes per piece (y, size, tile word, x)
			const uint32 mapping = rom.read32(sprite.mAttributeTable);
			const uint16 attributes = rom.read16(sprite.mAttributeTable + 4);
			if (!rom.valid(mapping + sprite.mFrame * 2, 2))
				return false;
			const uint32 frameAddress = mapping + rom.read16(mapping + sprite.mFrame * 2);
			const int count = rom.read16(frameAddress);
			if (count <= 0 || count > 64 || !rom.valid(frameAddress + 2, count * 6))
				return false;

			struct Piece { int x, y, width, height; uint16 word; };
			std::vector<Piece> pieces;
			int x0 = 0x7fff, y0 = 0x7fff, x1 = -0x7fff, y1 = -0x7fff;
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
				pieces.push_back(piece);
				x0 = std::min(x0, piece.x);
				y0 = std::min(y0, piece.y);
				x1 = std::max(x1, piece.x + piece.width * 8);
				y1 = std::max(y1, piece.y + piece.height * 8);
			}
			if (x1 - x0 <= 0 || y1 - y0 <= 0 || x1 - x0 > 256 || y1 - y0 > 256)
				return false;

			outBitmap.create(x1 - x0, y1 - y0, 0);
			for (auto it = pieces.rbegin(); it != pieces.rend(); ++it)		// The first piece is in front
			{
				const Piece& piece = *it;
				const int tile = piece.word & 0x7ff;
				const uint32* palette = palettes[(piece.word >> 13) & 3];
				const bool flipX = (piece.word & 0x800) != 0;
				const bool flipY = (piece.word & 0x1000) != 0;
				for (int tx = 0; tx < piece.width; ++tx)
				{
					for (int ty = 0; ty < piece.height; ++ty)
					{
						const int index = tile + tx * piece.height + ty;		// Tiles go down the columns first
						for (int py = 0; py < 8; ++py)
						{
							for (int px = 0; px < 8; ++px)
							{
								const uint8 byte = vram.mData[(index * 32 + py * 4 + px / 2) & 0xffff];
								const uint8 color = (px & 1) ? (byte & 0x0f) : (byte >> 4);
								if (color == 0)
									continue;
								const int dx = flipX ? (piece.width * 8 - 1 - (tx * 8 + px)) : (tx * 8 + px);
								const int dy = flipY ? (piece.height * 8 - 1 - (ty * 8 + py)) : (ty * 8 + py);
								*outBitmap.getPixelPointer(piece.x - x0 + dx, piece.y - y0 + dy) = palette[color];
							}
						}
					}
				}
			}
			return true;
		}
	}
}

#endif
