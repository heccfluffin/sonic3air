/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: QR codes (see the header). Follows the QR code standard (ISO/IEC 18004) the way Project Nayuki's
*	well-known "QR Code generator" explains it, kept to what short addresses need.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/platform/3ds/N3DSQRCode.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>


namespace n3ds
{
	namespace qrcode
	{
		namespace
		{
			const int MAX_VERSION = 6;		// No version information blocks needed up to here

			// Per version 1 to 6: error correction codewords per block and number of blocks, for levels L and M
			const int ECC_PER_BLOCK_L[MAX_VERSION + 1] = { 0, 7, 10, 15, 20, 26, 18 };
			const int BLOCKS_L[MAX_VERSION + 1]		   = { 0, 1, 1, 1, 1, 1, 2 };
			const int ECC_PER_BLOCK_M[MAX_VERSION + 1] = { 0, 10, 16, 26, 18, 24, 16 };
			const int BLOCKS_M[MAX_VERSION + 1]		   = { 0, 1, 1, 1, 2, 2, 4 };

			int getNumRawDataModules(int version)
			{
				int result = (16 * version + 128) * version + 64;
				if (version >= 2)
				{
					const int numAlign = version / 7 + 2;
					result -= (25 * numAlign - 10) * numAlign - 55;
				}
				return result;
			}

			uint8_t gfMultiply(uint8_t x, uint8_t y)
			{
				int z = 0;
				for (int i = 7; i >= 0; --i)
				{
					z = (z << 1) ^ ((z >> 7) * 0x11d);
					z ^= ((y >> i) & 1) * x;
				}
				return (uint8_t)z;
			}

			std::vector<uint8_t> reedSolomonRemainder(const std::vector<uint8_t>& data, int degree)
			{
				// Generator polynomial: the product of (x - 2^i) for i = 0 to degree - 1
				std::vector<uint8_t> divisor((size_t)degree, 0);
				divisor[(size_t)degree - 1] = 1;
				uint8_t root = 1;
				for (int i = 0; i < degree; ++i)
				{
					for (size_t j = 0; j < divisor.size(); ++j)
					{
						divisor[j] = gfMultiply(divisor[j], root);
						if (j + 1 < divisor.size())
							divisor[j] ^= divisor[j + 1];
					}
					root = gfMultiply(root, 0x02);
				}

				std::vector<uint8_t> result((size_t)degree, 0);
				for (uint8_t value : data)
				{
					const uint8_t factor = value ^ result[0];
					result.erase(result.begin());
					result.push_back(0);
					for (size_t i = 0; i < result.size(); ++i)
						result[i] ^= gfMultiply(divisor[i], factor);
				}
				return result;
			}

			struct Builder
			{
				int mSize;
				std::vector<bool> mModules;
				std::vector<bool> mIsFunction;

				explicit Builder(int version) :
					mSize(version * 4 + 17),
					mModules((size_t)(mSize * mSize), false),
					mIsFunction((size_t)(mSize * mSize), false)
				{}

				bool get(int x, int y) const  { return mModules[(size_t)(y * mSize + x)]; }
				void set(int x, int y, bool dark)  { mModules[(size_t)(y * mSize + x)] = dark; }
				void setFunction(int x, int y, bool dark)
				{
					mModules[(size_t)(y * mSize + x)] = dark;
					mIsFunction[(size_t)(y * mSize + x)] = true;
				}

				void drawFinder(int cx, int cy)
				{
					for (int dy = -4; dy <= 4; ++dy)
					{
						for (int dx = -4; dx <= 4; ++dx)
						{
							const int x = cx + dx;
							const int y = cy + dy;
							if (x < 0 || y < 0 || x >= mSize || y >= mSize)
								continue;
							const int distance = std::max(std::abs(dx), std::abs(dy));
							setFunction(x, y, distance != 2 && distance != 4);
						}
					}
				}

				void drawAlignment(int cx, int cy)
				{
					for (int dy = -2; dy <= 2; ++dy)
						for (int dx = -2; dx <= 2; ++dx)
							setFunction(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
				}

				void drawFunctionPatterns(int version)
				{
					for (int i = 0; i < mSize; ++i)
					{
						setFunction(6, i, i % 2 == 0);
						setFunction(i, 6, i % 2 == 0);
					}
					drawFinder(3, 3);
					drawFinder(mSize - 4, 3);
					drawFinder(3, mSize - 4);
					if (version >= 2)
						drawAlignment(mSize - 7, mSize - 7);	// Up to version 6 there's only this one
					drawFormatBits(0, 0);						// Reserved for now, the real ones come with the mask
				}

				void drawFormatBits(int eccFormatBits, int mask)
				{
					const int data = (eccFormatBits << 3) | mask;
					int remainder = data;
					for (int i = 0; i < 10; ++i)
						remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537);
					const int bits = ((data << 10) | remainder) ^ 0x5412;
					const auto bit = [bits](int i)  { return ((bits >> i) & 1) != 0; };

					for (int i = 0; i <= 5; ++i)
						setFunction(8, i, bit(i));
					setFunction(8, 7, bit(6));
					setFunction(8, 8, bit(7));
					setFunction(7, 8, bit(8));
					for (int i = 9; i < 15; ++i)
						setFunction(14 - i, 8, bit(i));
					for (int i = 0; i < 8; ++i)
						setFunction(mSize - 1 - i, 8, bit(i));
					for (int i = 8; i < 15; ++i)
						setFunction(8, mSize - 15 + i, bit(i));
					setFunction(8, mSize - 8, true);		// Always dark
				}

				void drawCodewords(const std::vector<uint8_t>& data)
				{
					size_t i = 0;
					for (int right = mSize - 1; right >= 1; right -= 2)
					{
						if (right == 6)
							right = 5;
						for (int vertical = 0; vertical < mSize; ++vertical)
						{
							for (int j = 0; j < 2; ++j)
							{
								const int x = right - j;
								const bool upward = ((right + 1) & 2) == 0;
								const int y = upward ? (mSize - 1 - vertical) : vertical;
								if (!mIsFunction[(size_t)(y * mSize + x)] && i < data.size() * 8)
								{
									set(x, y, ((data[i >> 3] >> (7 - (int)(i & 7))) & 1) != 0);
									++i;
								}
							}
						}
					}
				}

				void applyMask(int mask)
				{
					for (int y = 0; y < mSize; ++y)
					{
						for (int x = 0; x < mSize; ++x)
						{
							bool invert = false;
							switch (mask)
							{
								case 0:  invert = (x + y) % 2 == 0;  break;
								case 1:  invert = y % 2 == 0;  break;
								case 2:  invert = x % 3 == 0;  break;
								case 3:  invert = (x + y) % 3 == 0;  break;
								case 4:  invert = (x / 3 + y / 2) % 2 == 0;  break;
								case 5:  invert = x * y % 2 + x * y % 3 == 0;  break;
								case 6:  invert = (x * y % 2 + x * y % 3) % 2 == 0;  break;
								default: invert = ((x + y) % 2 + x * y % 3) % 2 == 0;  break;
							}
							if (invert && !mIsFunction[(size_t)(y * mSize + x)])
								set(x, y, !get(x, y));
						}
					}
				}

				// How hard the code is to read with this mask (the standard's penalty rules, lower is better)
				int getPenalty() const
				{
					int penalty = 0;
					for (int pass = 0; pass < 2; ++pass)
					{
						for (int a = 0; a < mSize; ++a)
						{
							int runLength = 0;
							bool runColor = false;
							for (int b = 0; b < mSize; ++b)
							{
								const bool dark = (pass == 0) ? get(b, a) : get(a, b);
								if (b > 0 && dark == runColor)
								{
									++runLength;
									if (runLength == 5)
										penalty += 3;
									else if (runLength > 5)
										++penalty;
								}
								else
								{
									runColor = dark;
									runLength = 1;
								}

								// Finder-like 1:1:3:1:1 with four light modules on one side
								if (b + 10 < mSize)
								{
									static const bool PATTERN_A[11] = { true, false, true, true, true, false, true, false, false, false, false };
									static const bool PATTERN_B[11] = { false, false, false, false, true, false, true, true, true, false, true };
									bool matchA = true;
									bool matchB = true;
									for (int k = 0; k < 11; ++k)
									{
										const bool module = (pass == 0) ? get(b + k, a) : get(a, b + k);
										matchA = matchA && (module == PATTERN_A[k]);
										matchB = matchB && (module == PATTERN_B[k]);
									}
									if (matchA)
										penalty += 40;
									if (matchB)
										penalty += 40;
								}
							}
						}
					}
					int dark = 0;
					for (int y = 0; y < mSize; ++y)
					{
						for (int x = 0; x < mSize; ++x)
						{
							if (get(x, y))
								++dark;
							if (x + 1 < mSize && y + 1 < mSize && get(x, y) == get(x + 1, y) && get(x, y) == get(x, y + 1) && get(x, y) == get(x + 1, y + 1))
								penalty += 3;
						}
					}
					const int total = mSize * mSize;
					const int k = (std::abs(dark * 20 - total * 10) + total - 1) / total - 1;
					return penalty + std::max(k, 0) * 10;
				}
			};
		}

		Code encode(const std::string& text)
		{
			Code code;

			// The smallest version that fits, with level M if possible
			int version = 0;
			bool levelM = true;
			for (int pass = 0; pass < 2 && version == 0; ++pass)
			{
				levelM = (pass == 0);
				for (int v = 1; v <= MAX_VERSION; ++v)
				{
					const int dataCodewords = getNumRawDataModules(v) / 8 - (levelM ? ECC_PER_BLOCK_M[v] * BLOCKS_M[v] : ECC_PER_BLOCK_L[v] * BLOCKS_L[v]);
					if ((int)text.size() + 2 <= dataCodewords)		// Mode and length: 12 bits, the terminator up to 4 more
					{
						version = v;
						break;
					}
				}
			}
			if (version == 0)
				return code;

			const int eccPerBlock = levelM ? ECC_PER_BLOCK_M[version] : ECC_PER_BLOCK_L[version];
			const int numBlocks = levelM ? BLOCKS_M[version] : BLOCKS_L[version];
			const int rawCodewords = getNumRawDataModules(version) / 8;
			const int dataCodewords = rawCodewords - eccPerBlock * numBlocks;

			// Data bits: byte mode, the length, the bytes, a terminator, then padding
			std::vector<bool> bits;
			const auto append = [&bits](uint32_t value, int count)
			{
				for (int i = count - 1; i >= 0; --i)
					bits.push_back(((value >> i) & 1) != 0);
			};
			append(0x4, 4);
			append((uint32_t)text.size(), 8);
			for (const char c : text)
				append((uint8_t)c, 8);
			const int capacityBits = dataCodewords * 8;
			append(0, std::min(4, capacityBits - (int)bits.size()));
			append(0, (8 - (int)(bits.size() % 8)) % 8);
			for (uint8_t pad = 0xec; (int)bits.size() < capacityBits; pad ^= 0xec ^ 0x11)
				append(pad, 8);

			std::vector<uint8_t> data((size_t)dataCodewords, 0);
			for (size_t i = 0; i < bits.size(); ++i)
				data[i >> 3] |= (uint8_t)(bits[i] ? (0x80 >> (i & 7)) : 0);

			// Split into blocks, add error correction to each, then interleave
			const int numShortBlocks = numBlocks - rawCodewords % numBlocks;
			const int shortBlockLength = rawCodewords / numBlocks;
			std::vector<std::vector<uint8_t>> blocks;
			size_t position = 0;
			for (int i = 0; i < numBlocks; ++i)
			{
				const size_t length = (size_t)(shortBlockLength - eccPerBlock + (i < numShortBlocks ? 0 : 1));
				std::vector<uint8_t> block(data.begin() + (ptrdiff_t)position, data.begin() + (ptrdiff_t)(position + length));
				position += length;
				const std::vector<uint8_t> ecc = reedSolomonRemainder(block, eccPerBlock);
				if (i < numShortBlocks)
					block.push_back(0);		// A gap, skipped when interleaving
				block.insert(block.end(), ecc.begin(), ecc.end());
				blocks.push_back(block);
			}
			std::vector<uint8_t> codewords;
			for (size_t i = 0; i < blocks[0].size(); ++i)
			{
				for (int j = 0; j < numBlocks; ++j)
				{
					if (i != (size_t)(shortBlockLength - eccPerBlock) || j >= numShortBlocks)
						codewords.push_back(blocks[(size_t)j][i]);
				}
			}

			// Draw it, with each of the 8 masks, and keep the best one
			const int eccFormatBits = levelM ? 0 : 1;
			Builder base(version);
			base.drawFunctionPatterns(version);
			base.drawCodewords(codewords);
			int bestPenalty = -1;
			for (int mask = 0; mask < 8; ++mask)
			{
				Builder candidate = base;
				candidate.applyMask(mask);
				candidate.drawFormatBits(eccFormatBits, mask);
				const int penalty = candidate.getPenalty();
				if (bestPenalty < 0 || penalty < bestPenalty)
				{
					bestPenalty = penalty;
					code.mSize = candidate.mSize;
					code.mModules = candidate.mModules;
				}
			}
			return code;
		}
	}
}
