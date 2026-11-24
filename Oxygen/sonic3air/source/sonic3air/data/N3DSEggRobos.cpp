/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: Egg Robo patrols.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"

#if defined(PLATFORM_3DS)

#include "sonic3air/data/N3DSEggRobos.h"
#include "sonic3air/audio/AudioOut.h"

#include "oxygen/application/Application.h"
#include "oxygen/application/video/VideoOut.h"
#include "oxygen/platform/3ds/N3DSChallenges.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/rendering/parts/SpriteManager.h"
#include "oxygen/rendering/parts/palette/Palette.h"
#include "oxygen/rendering/sprite/PaletteSprite.h"
#include "oxygen/rendering/utils/Kosinski.h"
#include "oxygen/rendering/utils/RenderUtils.h"
#include "oxygen/resources/SpriteCollection.h"
#include "oxygen/simulation/CodeExec.h"
#include "oxygen/simulation/EmulatorInterface.h"
#include "oxygen/simulation/Simulation.h"

#include <lemon/program/DataType.h>
#include <3ds.h>


namespace n3ds
{
	namespace eggrobos
	{
		namespace
		{
			const int MAX_ROBOS = 2;
			const int MAX_LASERS = 4;
			const int HOVER_DISTANCE = 120;		// Pixels to the side of the character
			const int CHARGE_FRAMES = 31;		// Like the original: the laser blinks at the gun for half a second...
			const int COOLDOWN_FRAMES = 95;		// ...and then the gun needs a bit more than 1.5 seconds
			const int SHOTS = 3;				// Before flying off again
			const int MAX_AGE = 15 * 60;		// Frames, also flying off after that
			const int LASER_SPEED = 8;
			const uint8 SFX_LASER = 0x5e;
			const uint8 SFX_DEFLECT = 0x3d;

			// The Egg Robo in the ROM: its art (Kosinski compressed) and mappings; sprite frames 1 body, 2 gun arm, 3 body with the
			// jet flame, 4 to 6 legs, 7 laser
			const uint32 ART_ADDRESS = 0x17b17e;
			const uint32 MAPPINGS_ADDRESS = 0x184f34;
			const int FRAME_BODY = 1;
			const int FRAME_ARM = 2;
			const int FRAME_BODY_FLAME = 3;
			const int FRAME_LEGS = 4;
			const int FRAME_LASER = 7;

			enum class State
			{
				FLY_IN,
				HOVER,
				CHARGE,
				LEAVE
			};

			struct Robo
			{
				bool mActive = false;
				State mState = State::FLY_IN;
				float mX = 0.0f;
				float mY = 0.0f;
				float mVelocityX = 0.0f;
				float mVelocityY = 0.0f;
				int mSide = 1;			// Which side of the character it hovers at
				int mFacing = -1;		// -1 left (as in the ROM), 1 right
				int mTimer = 0;
				int mCooldown = 60;
				int mShots = 0;
				int mAge = 0;
				int mTargetY = 0;
			};

			struct Laser
			{
				bool mActive = false;
				int mX = 0;
				int mY = 0;
				int mDirection = 1;
			};

			// Red Eye, the Death Egg's eye (its first boss), as a security camera: it hovers at the top of the screen sweeping its
			// searchlight to and fro, and if the character walks into the light it calls the Egg Robos
			const uint32 RED_EYE_ART_ADDRESS = 0x1805a0;
			const uint32 RED_EYE_MAPPINGS_ADDRESS = 0x184fba;
			const int RED_EYE_FRAME_BODY = 0;			// Its core: domes at the top and bottom, cannons at the sides...
			const int RED_EYE_FRAME_EYE = 2;			// ...and the eye in the middle
			const int RED_EYE_FRAME_BEAM = 27;			// 27 to 34: the beam, 24 to 136 pixels long
			const int RED_EYE_SCAN_FRAMES = 12 * 60;
			const int RED_EYE_ALERT_FRAMES = 90;

			enum class EyeState
			{
				ARRIVE,
				SCAN,
				ALERT,
				LEAVE
			};

			struct RedEye
			{
				bool mActive = false;
				EyeState mState = EyeState::ARRIVE;
				float mX = 0.0f;
				float mY = 0.0f;
				int mDirection = 1;
				int mTimer = 0;
				int mAge = 0;
				int mBeamLength = 0;		// Down to the ground (or as long as the beam gets)
			};

			RedEye gRedEye;
			uint64 gRedEyeKeys[35] = { 0 };
			PaletteBase gRedEyePalette;
			bool gRedEyeReady = false;

			Robo gRobos[MAX_ROBOS];
			Laser gLasers[MAX_LASERS];
			uint64 gSpriteKeys[8] = { 0 };
			PaletteBase gPalette;
			bool gPaletteReady = false;
			int gSpawnTimer = -1;
			uint16 gZoneAct = 0xffff;
			bool gHasFrameNumber = false;
			uint16 gFrameNumber = 0;
			int gActFrames = 0;

			EmulatorInterface& emu()  { return EmulatorInterface::instance(); }

			uint32 random(uint32 range)
			{
				static uint32 state = (uint32)svcGetSystemTick() | 1;
				state ^= state << 13;
				state ^= state >> 17;
				state ^= state << 5;
				return (range == 0) ? 0 : ((state >> 4) % range);
			}

			// Runs one of the game's script functions now, between frames; the game's registers are put back afterwards
			void callGameScript(const char* functionName, uint32 a0, CodeExec::FunctionExecData* execData = nullptr)
			{
				EmulatorInterface& emulatorInterface = emu();
				uint32 registers[16];
				for (size_t k = 0; k < 16; ++k)
					registers[k] = emulatorInterface.getRegister(k);
				emulatorInterface.getRegister(EmulatorInterface::Register::A0) = a0;
				emulatorInterface.getRegister(EmulatorInterface::Register::A2) = a0;
				Application::instance().getSimulation().getCodeExec().executeScriptFunction(functionName, false, execData);
				for (size_t k = 0; k < 16; ++k)
					emulatorInterface.getRegister(k) = registers[k];
			}

			// The level's 16 x 16 block at a position: its upper bits say how solid it is (0x1000 / 0x4000 top, 0x2000 / 0x8000
			// the sides and bottom, for the two collision paths)
			uint16 getBlock(int x, int y)
			{
				EmulatorInterface& emulatorInterface = emu();
				if (x < 0 || y < 0)
					return 0;
				const int widthInChunks = emulatorInterface.readMemory16(0xffff8000);
				const int heightInChunks = emulatorInterface.readMemory16(0xffff8004);
				if ((x >> 7) >= widthInChunks || (y >> 7) >= std::max(heightInChunks, 1))
					return 0;
				const uint32 row = emulatorInterface.readMemory16(0xffff8008 + ((y >> 7) & 0x1f) * 4);
				const uint8 chunk = emulatorInterface.readMemory8(0xffff0000 + row + (x >> 7));
				const uint32 chunkData = 0xffff0000 + emulatorInterface.readMemory16(0x00f02a + chunk * 2);
				return emulatorInterface.readMemory16(chunkData + ((y >> 4) & 7) * 16 + ((x >> 4) & 7) * 2);
			}

			inline bool isWall(int x, int y)		{ return (getBlock(x, y) & 0xa000) != 0; }		// Something a laser can't go through
			inline bool isSolid(int x, int y)		{ return (getBlock(x, y) & 0xf000) != 0; }

			// Room for an Egg Robo: its body clear of anything solid (the legs may dangle over the ground, like in Sky Sanctuary)
			bool isClearSpot(int x, int y)
			{
				for (int dy = -24; dy <= 8; dy += 16)
				{
					for (int dx = -24; dx <= 24; dx += 16)
					{
						if (isSolid(x + dx, y + dy))
							return false;
					}
				}
				return true;
			}

			// Nothing solid between its gun and the character, at the gun's height
			bool hasLineOfFire(int fromX, int toX, int y)
			{
				const int step = (toX >= fromX) ? 12 : -12;
				for (int x = fromX; (step > 0) ? (x < toX) : (x > toX); x += step)
				{
					if (isWall(x, y))
						return false;
				}
				return true;
			}

			// Builds a sprite frame from art compressed in the ROM (Kosinski, in modules of up to 4 KB) and its mappings. Done here
			// rather than with the engine's own ROM sprite setup: that one only gets the first module right, and art in several
			// modules (like Red Eye's) overran its buffer.
			uint64 setupSprite(uint32 artAddress, uint32 mappingsAddress, int frame, uint8 (*recolor)(int x, int y, uint8 color) = nullptr)
			{
				EmulatorInterface& emulatorInterface = emu();
				static uint32 cachedArtAddress = 0;
				static std::vector<RenderUtils::PatternPixelContent> patternBuffer;
				if (cachedArtAddress != artAddress)
				{
					// Unpack the art: module by module, each starting on a 16 bytes boundary
					const uint16 totalSize = emulatorInterface.readMemory16(artAddress);
					const uint32 start = artAddress + 2;
					uint32 input = start;
					std::vector<uint8> art;
					art.reserve(totalSize);
					static uint8 buffer[0x4000];		// A module is never more than 0x1000 bytes; lots of room to spare
					while (art.size() < totalSize)
					{
						uint8* pointer = buffer;
						Kosinski::decompress(emulatorInterface, pointer, input);
						const size_t bytes = (size_t)(pointer - buffer);
						if (bytes == 0 || bytes > 0x1000)
						{
							RMX_LOG_INFO("Egg Robos: unexpected art data at " << rmx::hexString(artAddress, 6));
							break;
						}
						art.insert(art.end(), buffer, buffer + bytes);
						input = start + ((input - start + 15) & ~15u);
					}
					art.resize(art.size() / 32 * 32);
					patternBuffer.clear();
					RenderUtils::expandMultiplePatternDataFromROM(patternBuffer, art.data(), (uint32)(art.size() / 32));
					cachedArtAddress = artAddress;
				}

				std::vector<RenderUtils::SinglePattern> patterns;
				const uint32 mappingAddress = mappingsAddress + emulatorInterface.readMemory16(mappingsAddress + frame * 2);
				const int count = emulatorInterface.readMemory16(mappingAddress);
				for (int k = 0; k < count && k < 64; ++k)
					RenderUtils::fillPatternsFromSpriteData(patterns, emulatorInterface.getRom() + mappingAddress + 2 + k * 6, patternBuffer, 0);

				const uint64 key = rmx::getMurmur2_64(String(0, "n3ds_rom_sprite_%06x_%02x", artAddress, frame));
				SpriteCollection::Item& item = SpriteCollection::instance().getOrCreatePaletteSprite(key);
				PaletteSprite& sprite = *static_cast<PaletteSprite*>(item.mSprite);
				sprite.createFromSpritePatterns(patterns);
				if (nullptr != recolor)
				{
					// Pixels moved to other palette lines, by where they are in the sprite (relative to its pivot)
					PaletteBitmap& bitmap = sprite.accessBitmap();
					for (int y = 0; y < bitmap.getHeight(); ++y)
					{
						for (int x = 0; x < bitmap.getWidth(); ++x)
						{
							uint8& pixel = *bitmap.getPixelPointer(x, y);
							if (pixel != 0)
								pixel = recolor(x + sprite.mOffset.x, y + sprite.mOffset.y, pixel);
						}
					}
				}
				++item.mChangeCounter;
				return key;
			}

			void setupGraphics()
			{
				EmulatorInterface& emulatorInterface = emu();
				SpriteCollection& spriteCollection = SpriteCollection::instance();
				for (int frame = FRAME_BODY; frame <= FRAME_LASER; ++frame)
				{
					if (gSpriteKeys[frame] != 0 && spriteCollection.hasSprite(gSpriteKeys[frame]))
						continue;
					gSpriteKeys[frame] = setupSprite(ART_ADDRESS, MAPPINGS_ADDRESS, frame);
				}

				if (!gPaletteReady)
				{
					// Their colors as in Sky Sanctuary: palette line 0 (Sonic's, for the jet flame) and the zone's lines 1 to 3,
					// both from the game's own palette tables
					static const uint8 LEVELS[8] = { 0x00, 0x24, 0x49, 0x6d, 0x92, 0xb6, 0xdb, 0xff };
					const auto convert = [](uint16 md) -> uint32
					{
						return 0xff000000 | ((uint32)LEVELS[(md >> 9) & 7] << 16) | ((uint32)LEVELS[(md >> 5) & 7] << 8) | (uint32)LEVELS[(md >> 1) & 7];
					};
					uint32 colors[64];
					const uint32 sonicPalette = emulatorInterface.readMemory32(0x0a872c + 0x03 * 8);
					const uint32 skySanctuaryPalette = emulatorInterface.readMemory32(0x0a872c + 0x1e * 8);
					for (int k = 0; k < 16; ++k)
						colors[k] = convert(emulatorInterface.readMemory16(sonicPalette + k * 2));
					for (int k = 0; k < 48; ++k)
						colors[16 + k] = convert(emulatorInterface.readMemory16(skySanctuaryPalette + k * 2));
					for (int line = 0; line < 4; ++line)
						colors[line * 16] = 0;		// The first color of each line is see-through
					gPalette.initPalette(0x5ad0e9b0u, 64, BitFlagSet<PaletteBase::Properties>(), "n3ds_eggrobo");
					gPalette.writeRawColors(colors, 64);
					gPaletteReady = true;
				}
			}

			void setupRedEyeGraphics()
			{
				EmulatorInterface& emulatorInterface = emu();
				SpriteCollection& spriteCollection = SpriteCollection::instance();
				// Its sprite is in one gray color ramp; colored like its familiar look: a blue-steel body (palette line 1), red domes
				// and eye (line 2), and the beam with red edges (line 3)
				static const int FRAMES[] = { 0, 2, 27, 28, 29, 30, 31, 32, 33, 34 };
				for (int frame : FRAMES)
				{
					if (gRedEyeKeys[frame] != 0 && spriteCollection.hasSprite(gRedEyeKeys[frame]))
						continue;
					uint8 (*recolor)(int, int, uint8) = nullptr;
					if (frame == RED_EYE_FRAME_BODY)
						recolor = [](int x, int y, uint8 color) -> uint8  { return (y <= -17 || y >= 17) ? (uint8)(color + 0x10) : color; };
					else if (frame == RED_EYE_FRAME_EYE)
						recolor = [](int x, int y, uint8 color) -> uint8  { return (uint8)(color + 0x10); };
					else
						recolor = [](int x, int y, uint8 color) -> uint8  { return (uint8)(color + 0x20); };
					gRedEyeKeys[frame] = setupSprite(RED_EYE_ART_ADDRESS, RED_EYE_MAPPINGS_ADDRESS, frame, recolor);
				}
				if (!gRedEyeReady)
				{
					// The boss fight's colors (the palette line it loads), with its cyan eye and beam edges turned red - it's Red Eye
					static const uint8 LEVELS[8] = { 0x00, 0x24, 0x49, 0x6d, 0x92, 0xb6, 0xdb, 0xff };
					uint32 colors[64] = { 0 };
					for (int k = 1; k < 16; ++k)
					{
						const uint16 md = emulatorInterface.readMemory16(0x07f01c + k * 2);
						const uint32 color = 0xff000000 | ((uint32)LEVELS[(md >> 9) & 7] << 16) | ((uint32)LEVELS[(md >> 5) & 7] << 8) | (uint32)LEVELS[(md >> 1) & 7];
						colors[16 + k] = colors[32 + k] = colors[48 + k] = color;
					}
					// Its gray ramp (colors 9 to 14) as blue steel and as red; the beam's cyan edges red too (all ABGR)
					static const uint32 STEEL[6] = { 0xffffecd8, 0xfff0c49c, 0xffd89664, 0xffb06c3c, 0xff7c4222, 0xff441c0e };
					static const uint32 RED[6]   = { 0xffa0b4ff, 0xff465af4, 0xff2424d0, 0xff12129c, 0xff080864, 0xff040430 };
					for (int k = 0; k < 6; ++k)
					{
						colors[16 + 9 + k] = STEEL[k];
						colors[32 + 9 + k] = RED[k];
					}
					colors[48 + 7] = 0xff2040ff;
					colors[48 + 8] = 0xff0000c0;
					colors[48 + 9] = 0xff000080;
					gRedEyePalette.initPalette(0x5ad0e9b1u, 64, BitFlagSet<PaletteBase::Properties>(), "n3ds_redeye");
					gRedEyePalette.writeRawColors(colors, 64);
					gRedEyeReady = true;
				}
			}

			void hurtCharacter(bool fromRight)
			{
				EmulatorInterface& emulatorInterface = emu();
				callGameScript("Character.GettingHurt", 0xffffb000);
				if (emulatorInterface.readMemory8(0xffffb005) == 0x04)		// base_state.HURT: thrown away from what hit
				{
					const int speed = std::abs((int)(int16)emulatorInterface.readMemory16(0xffffb018));
					emulatorInterface.writeMemory16(0xffffb018, (uint16)(int16)(fromRight ? -speed : speed));
				}
			}

			bool canBeHurt()
			{
				EmulatorInterface& emulatorInterface = emu();
				return emulatorInterface.readMemory8(0xffffb005) == 0x02 && emulatorInterface.readMemory8(0xffffb034) == 0 &&		// char.base_state, char.invuln.countdown
					   emulatorInterface.readMemory8(0xffffb02e) == 0 && (emulatorInterface.readMemory8(0xffffb02b) & 0x02) == 0 &&	// char.control_flags, invincible
					   emulatorInterface.readMemory8(0xfffffe19) == 0;		// super.active
			}

			// Popped like a badnik: the game's own explosion object (with its points, the score chain and a freed animal), and the
			// character bounces off
			void pop(Robo& robo)
			{
				EmulatorInterface& emulatorInterface = emu();
				for (uint32 address = 0xffffb0de + 0x4a; address < 0xffffb0de + 0x4a * 0x5a; address += 0x4a)
				{
					if (emulatorInterface.readMemory32(address) != 0)
						continue;
					for (uint32 k = 0; k < 0x4a; ++k)
						emulatorInterface.writeMemory8(address + k, 0);
					emulatorInterface.writeMemory32(address, 0x01e5e0);		// SmallExplosion.BaseUpdate
					emulatorInterface.writeMemory16(address + 0x10, (uint16)roundToInt(robo.mX));
					emulatorInterface.writeMemory16(address + 0x14, (uint16)roundToInt(robo.mY));

					CodeExec::FunctionExecData execData;
					execData.mParams.mReturnType = &lemon::PredefinedDataTypes::VOID;
					execData.addParam(lemon::PredefinedDataTypes::UINT_32, address);
					execData.addParam(lemon::PredefinedDataTypes::UINT_32, 0x010216);		// The badniks' score table
					callGameScript("Enemy.addScore", 0xffffb000, &execData);
					break;
				}

				const int characterY = emulatorInterface.readMemory16(0xffffb014);
				int velocityY = (int16)emulatorInterface.readMemory16(0xffffb01a);		// char.velocity.y
				if (velocityY < 0)
					velocityY += 0x100;
				else if (characterY >= roundToInt(robo.mY))
					velocityY -= 0x100;
				else
					velocityY = -velocityY;
				emulatorInterface.writeMemory16(0xffffb01a, (uint16)(int16)velocityY);
				robo.mActive = false;
				RMX_LOG_INFO("Egg Robo: popped");
			}

			bool isAttacking()
			{
				// Like hitting a badnik: rolling or jumping, spindashing, the insta-shield, a shield move or gliding, invincible or Super
				EmulatorInterface& emulatorInterface = emu();
				const uint8 animation = emulatorInterface.readMemory8(0xffffb020);		// char.state: 0x02 rolling (and jumping)
				const uint8 character = emulatorInterface.readMemory8(0xffffb038);
				return animation == 0x02 || emulatorInterface.readMemory8(0xffffb03d) != 0 ||
					   (emulatorInterface.readMemory8(0xffffb02f) == 1 && character != 1) ||
					   (emulatorInterface.readMemory8(0xffffb02b) & 0x02) != 0 || emulatorInterface.readMemory8(0xfffffe19) != 0;
			}

			void fireLaser(const Robo& robo)
			{
				for (Laser& laser : gLasers)
				{
					if (laser.mActive)
						continue;
					laser.mActive = true;
					laser.mDirection = robo.mFacing;
					laser.mX = roundToInt(robo.mX) + robo.mFacing * 39;		// At the gun's muzzle
					laser.mY = roundToInt(robo.mY) - 8;
					AudioOut::instance().playAudioDirect(SFX_LASER, AudioOut::SoundRegType::SOUND, AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_SOUND);
					return;
				}
			}

			bool trySpawn(int characterX, int characterY, int cameraX, int screenWidth, int forcedSide = 0)
			{
				// A clear spot at the character's height, preferably on the side they're running towards, else the other side,
				// else a bit higher or lower
				const bool facingLeft = (emu().readMemory8(0xffffb02a) & 0x01) != 0;		// char.flags: FACING_LEFT
				const int preferredSide = (forcedSide != 0) ? forcedSide : (random(10) < 7) ? (facingLeft ? -1 : 1) : (facingLeft ? 1 : -1);
				static const int HEIGHTS[3] = { 0, -32, 32 };
				for (int sideIndex = 0; sideIndex < 2; ++sideIndex)
				{
					const int side = (sideIndex == 0) ? preferredSide : -preferredSide;
					for (int height : HEIGHTS)
					{
						const int x = characterX + side * HOVER_DISTANCE;
						const int y = characterY - 4 + height;
						if (!isClearSpot(x, y))
							continue;

						for (Robo& robo : gRobos)
						{
							if (robo.mActive)
								continue;
							robo = Robo();
							robo.mActive = true;
							robo.mState = State::FLY_IN;
							robo.mSide = side;
							robo.mX = (float)((side > 0) ? (cameraX + screenWidth + 40) : (cameraX - 40));
							robo.mY = (float)(y - 56);
							robo.mFacing = (side > 0) ? -1 : 1;
							robo.mTargetY = height;
							RMX_LOG_INFO("Egg Robo: flying in from the " << ((side > 0) ? "right" : "left"));
							return true;
						}
						return false;
					}
				}
				return false;
			}

			void updateRobo(Robo& robo, int characterX, int characterY, int cameraX, int cameraY, int screenWidth, int screenHeight)
			{
				++robo.mAge;
				if (robo.mCooldown > 0)
					--robo.mCooldown;
				robo.mFacing = (characterX < robo.mX) ? -1 : 1;

				if (robo.mState != State::LEAVE)
				{
					// Done, or too far away: off it goes
					if (robo.mShots >= SHOTS || robo.mAge > MAX_AGE || std::abs(characterX - robo.mX) > 360 || std::abs(characterY - robo.mY) > 240)
					{
						robo.mState = State::LEAVE;
						robo.mVelocityY = -1.0f;
					}
				}

				if (robo.mState == State::LEAVE)
				{
					// Up and away, then gone
					robo.mVelocityY = std::max(robo.mVelocityY - 0.15f, -4.0f);
					robo.mVelocityX = (float)robo.mSide * 1.5f;
					robo.mX += robo.mVelocityX;
					robo.mY += robo.mVelocityY;
					if (robo.mY < cameraY - 80 || robo.mX < cameraX - 120 || robo.mX > cameraX + screenWidth + 120)
						robo.mActive = false;
					return;
				}

				// Its spot next to the character, bobbing a little; if that's in a wall, the other side
				int targetX = characterX + robo.mSide * HOVER_DISTANCE;
				int targetY = characterY - 4 + robo.mTargetY + roundToInt(std::sin(robo.mAge * 0.08f) * 2.0f);
				if (!isClearSpot(targetX, targetY))
				{
					if (isClearSpot(characterX - robo.mSide * HOVER_DISTANCE, targetY))
					{
						robo.mSide = -robo.mSide;
						targetX = characterX + robo.mSide * HOVER_DISTANCE;
					}
					else if (isClearSpot(targetX, characterY - 4))
					{
						robo.mTargetY = 0;
						targetY = characterY - 4;
					}
				}
				const float maxSpeed = (robo.mState == State::FLY_IN) ? 4.0f : (robo.mState == State::CHARGE) ? 0.5f : 2.5f;
				robo.mVelocityX = clamp(((float)targetX - robo.mX) * 0.08f, -maxSpeed, maxSpeed);
				robo.mVelocityY = clamp(((float)targetY - robo.mY) * 0.1f, -maxSpeed, maxSpeed);
				robo.mX += robo.mVelocityX;
				robo.mY += robo.mVelocityY;
				if (robo.mState == State::FLY_IN && std::abs(targetX - robo.mX) < 12.0f && std::abs(targetY - robo.mY) < 12.0f)
				{
					robo.mState = State::HOVER;
					robo.mCooldown = std::max(robo.mCooldown, 90);		// A moment to see it coming before the first shot
				}

				const int gunY = roundToInt(robo.mY) - 8;
				if (robo.mState == State::HOVER)
				{
					// The original aims when the character is no more than 8 pixels above or below it
					const int dy = characterY - roundToInt(robo.mY);
					if (robo.mCooldown == 0 && std::abs(dy) <= 10 && std::abs(characterX - robo.mX) < 256 &&
						hasLineOfFire(roundToInt(robo.mX) + robo.mFacing * 39, characterX, gunY))
					{
						robo.mState = State::CHARGE;
						robo.mTimer = CHARGE_FRAMES;
					}
				}
				else if (robo.mState == State::CHARGE)
				{
					--robo.mTimer;
					if (robo.mTimer <= 0)
					{
						fireLaser(robo);
						RMX_LOG_INFO("Egg Robo: fired (age " << robo.mAge << " frames)");
						++robo.mShots;
						robo.mCooldown = COOLDOWN_FRAMES;
						robo.mState = State::HOVER;
					}
				}

				// Touching the character: popped if they attack, otherwise a hit for them
				const int dx = characterX - roundToInt(robo.mX);
				const int dyBody = characterY - roundToInt(robo.mY) - 8;
				if (std::abs(dx) < 26 && std::abs(dyBody) < 38)
				{
					if (isAttacking())
						pop(robo);
					else if (canBeHurt())
					{
						RMX_LOG_INFO("Egg Robo: bumped into the character");
						hurtCharacter(dx < 0);
					}
				}
			}

			void spawnRedEye(int characterX, int cameraY)
			{
				gRedEye = RedEye();
				gRedEye.mActive = true;
				gRedEye.mState = EyeState::ARRIVE;
				// It arrives off to one side and sweeps its light towards the character: there's time to see it coming
				const int side = (random(2) == 0) ? -1 : 1;
				gRedEye.mX = (float)(characterX + side * (130 + (int)random(40)));
				gRedEye.mY = (float)(cameraY - 40);
				gRedEye.mDirection = -side;
				RMX_LOG_INFO("Red Eye: watching");
			}

			void updateRedEye(int characterX, int characterY, int cameraX, int cameraY, int screenWidth)
			{
				RedEye& eye = gRedEye;
				++eye.mAge;
				const float hoverY = (float)(cameraY + 40);		// Near the top of the screen
				if (eye.mState == EyeState::LEAVE)
				{
					eye.mY -= 3.0f;
					if (eye.mY < cameraY - 60)
						eye.mActive = false;
					return;
				}
				if (eye.mState == EyeState::ARRIVE)
				{
					eye.mY = std::min(eye.mY + 2.0f, hoverY);
					if (eye.mY >= hoverY)
					{
						eye.mState = EyeState::SCAN;
						eye.mTimer = RED_EYE_SCAN_FRAMES;
					}
				}
				else
				{
					eye.mY += clamp(hoverY - eye.mY, -3.0f, 3.0f);
				}

				// Its searchlight: straight down, until the ground
				const int eyeX = roundToInt(eye.mX);
				const int beamTop = roundToInt(eye.mY) + 28;
				eye.mBeamLength = 136;
				for (int d = 8; d <= 136; d += 8)
				{
					if (isSolid(eyeX, beamTop + d))
					{
						eye.mBeamLength = d;
						break;
					}
				}

				if (eye.mState == EyeState::SCAN)
				{
					// Sweeping to and fro around the character, within the screen
					eye.mX += (float)eye.mDirection * 1.25f;
					if ((eye.mDirection > 0 && (eye.mX > characterX + 140 || eye.mX > cameraX + screenWidth - 24)) ||
						(eye.mDirection < 0 && (eye.mX < characterX - 140 || eye.mX < cameraX + 24)))
						eye.mDirection = -eye.mDirection;

					// Walked into the light?
					if (std::abs(characterX - eyeX) < 14 && characterY > beamTop && characterY < beamTop + eye.mBeamLength + 12)
					{
						eye.mState = EyeState::ALERT;
						eye.mTimer = RED_EYE_ALERT_FRAMES;
						RMX_LOG_INFO("Red Eye: spotted the character");
					}
					else if (--eye.mTimer <= 0)
					{
						eye.mState = EyeState::LEAVE;
						RMX_LOG_INFO("Red Eye: saw nothing, moving on");
					}
				}
				else if (eye.mState == EyeState::ALERT)
				{
					// The alarm, then the Egg Robos come from both sides
					const int elapsed = RED_EYE_ALERT_FRAMES - eye.mTimer;
					if (elapsed % 30 == 0)
						AudioOut::instance().playAudioDirect(0x86, AudioOut::SoundRegType::SOUND, AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_SOUND);		// Launch Base's alarm
					if (elapsed == 45)
					{
						setupGraphics();
						trySpawn(characterX, characterY, cameraX, screenWidth, 1);
						trySpawn(characterX, characterY, cameraX, screenWidth, -1);
					}
					if (--eye.mTimer <= 0)
						eye.mState = EyeState::LEAVE;
				}

				// It can be popped too, if the character gets up there
				if (eye.mState != EyeState::LEAVE && std::abs(characterX - eyeX) < 32 && std::abs(characterY - roundToInt(eye.mY)) < 40 && isAttacking())
				{
					Robo asRobo;
					asRobo.mX = eye.mX;
					asRobo.mY = eye.mY;
					pop(asRobo);
					eye.mActive = false;
					RMX_LOG_INFO("Red Eye: popped");
				}
			}

			void updateLaser(Laser& laser, int characterX, int characterY, int cameraX, int screenWidth)
			{
				laser.mX += laser.mDirection * LASER_SPEED;
				if (laser.mX < cameraX - 64 || laser.mX > cameraX + screenWidth + 64 || isWall(laser.mX + laser.mDirection * 24, laser.mY))
				{
					laser.mActive = false;
					return;
				}
				if (std::abs(characterX - laser.mX) < 30 && std::abs(characterY - laser.mY) < 20)
				{
					EmulatorInterface& emulatorInterface = emu();
					if ((emulatorInterface.readMemory8(0xffffb02b) & 0x70) != 0)		// An elemental shield bounces it off
					{
						laser.mActive = false;
						AudioOut::instance().playAudioDirect(SFX_DEFLECT, AudioOut::SoundRegType::SOUND, AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_SOUND);
					}
					else if (canBeHurt())
					{
						laser.mActive = false;
						RMX_LOG_INFO("Egg Robo: the laser hit the character");
						hurtCharacter(laser.mDirection < 0);
					}
				}
			}

			void drawPart(int frame, int x, int y, bool flipX, uint16 renderQueue)
			{
				if (gSpriteKeys[frame] == 0)
					return;
				RenderParts::instance().getSpriteManager().drawCustomSprite(gSpriteKeys[frame], Vec2i(x, y), 0, flipX ? 0x01 : 0x00, renderQueue);
			}
		}

		void drawRedEye()
		{
			EmulatorInterface& emulatorInterface = emu();
			const RedEye& eye = gRedEye;
			const int x = roundToInt(eye.mX) - emulatorInterface.readMemory16(0xffffee80);
			const int y = roundToInt(eye.mY) - emulatorInterface.readMemory16(0xffffee84);
			SpriteManager& spriteManager = RenderParts::instance().getSpriteManager();
			spriteManager.setCustomSpritePaletteOverride(&gRedEyePalette, &gRedEyePalette);

			// The searchlight: see-through, flickering a little; brighter and red while the alarm goes
			if (eye.mState == EyeState::SCAN || eye.mState == EyeState::ALERT)
			{
				const int step = clamp((eye.mBeamLength - 24) / 16, 0, 7);
				const int frame = RED_EYE_FRAME_BEAM + step;
				const int beamTop = y + 28;
				const bool alert = (eye.mState == EyeState::ALERT);
				const float alpha = alert ? (((eye.mAge / 4) % 2) ? 0.7f : 0.45f) : (0.32f + 0.05f * std::sin(eye.mAge * 0.3f));
				const Color tint = alert ? Color(1.0f, 0.4f, 0.4f, alpha) : Color(1.0f, 1.0f, 1.0f, alpha);
				if (gRedEyeKeys[frame] != 0)
					spriteManager.drawCustomSprite(gRedEyeKeys[frame], Vec2i(x, beamTop + 8 + step * 8), 0x10, 0x00, 0x9dfe, tint);
			}

			// Its core, and the eye in the middle: dimming for a blink now and then, flashing when it spotted someone
			if (gRedEyeKeys[RED_EYE_FRAME_BODY] != 0)
				spriteManager.drawCustomSprite(gRedEyeKeys[RED_EYE_FRAME_BODY], Vec2i(x, y), 0x10, 0x00, 0x9e04);
			Color eyeTint = Color::WHITE;
			if (eye.mState == EyeState::ALERT)
				eyeTint = ((eye.mAge / 6) % 2) ? Color(1.8f, 1.2f, 1.2f, 1.0f) : Color::WHITE;
			else if ((eye.mAge % 180) < 8)
				eyeTint = Color(0.45f, 0.45f, 0.45f, 1.0f);
			if (gRedEyeKeys[RED_EYE_FRAME_EYE] != 0)
				spriteManager.drawCustomSprite(gRedEyeKeys[RED_EYE_FRAME_EYE], Vec2i(x, y - 4), 0x10, 0x00, 0x9e05, eyeTint);
			spriteManager.setCustomSpritePaletteOverride(nullptr, nullptr);
		}

		void clear()
		{
			for (Robo& robo : gRobos)
				robo.mActive = false;
			for (Laser& laser : gLasers)
				laser.mActive = false;
			gRedEye.mActive = false;
			gHasFrameNumber = false;
			gActFrames = 0;
		}

		void onControls(bool allowed)
		{
			EmulatorInterface& emulatorInterface = emu();
			const int setting = n3ds::challenges::getEggRoboSetting();
			const bool levelRunning = (emulatorInterface.readMemory8(0xfffff600) == 0x0c) && ((int8)emulatorInterface.readMemory8(0xfffff711) > 0);	// global.level_started
			const uint16 zoneAct = emulatorInterface.readMemory16(0xfffffe10);
			if (!allowed || !levelRunning || zoneAct != gZoneAct || setting <= 0)
			{
				if (gHasFrameNumber || gActFrames > 0)
					clear();
				gZoneAct = zoneAct;
				gSpawnTimer = -1;
				return;
			}

			// Only with new frames of the game, and not in cutscenes or while the character is out of the game
			const uint16 frameNumber = emulatorInterface.readMemory16(0xfffffe04);		// level.framecounter
			const bool newFrame = !gHasFrameNumber || frameNumber != gFrameNumber;
			gHasFrameNumber = true;
			gFrameNumber = frameNumber;
			if (!newFrame || (int8)emulatorInterface.readMemory8(0xfffff7ca) != 0)		// player1.control_override
				return;
			if (emulatorInterface.readMemory8(0xffffb005) >= 0x06)		// char.base_state: died
			{
				clear();
				gHasFrameNumber = true;
				return;
			}
			++gActFrames;

			const int characterX = emulatorInterface.readMemory16(0xffffb010);
			const int characterY = emulatorInterface.readMemory16(0xffffb014);
			const int cameraX = emulatorInterface.readMemory16(0xffffee80);
			const int cameraY = emulatorInterface.readMemory16(0xffffee84);
			const int screenWidth = VideoOut::instance().getScreenWidth();
			const int screenHeight = VideoOut::instance().getScreenHeight();
			const bool bossFight = (emulatorInterface.readMemory8(0xfffff7aa) != 0);		// level.boss_encounter

			// Now and then, a new one (never in boss fights, nor in the first seconds of an act)
			static const int MIN_SECONDS[4] = { 0, 90, 45, 20 };
			static const int MAX_SECONDS[4] = { 0, 150, 75, 35 };
			const int index = clamp(setting, 1, 3);
			if (gSpawnTimer < 0)
				gSpawnTimer = (MIN_SECONDS[index] + (int)random((uint32)(MAX_SECONDS[index] - MIN_SECONDS[index]))) * 60;
			if (gSpawnTimer > 0)
				--gSpawnTimer;
			if (gSpawnTimer == 0 && !bossFight && gActFrames > 10 * 60)
			{
				// Half of the time, Red Eye comes first to look for the character (when nothing else is around)
				bool anyRobo = false;
				for (const Robo& robo : gRobos)
					anyRobo = anyRobo || robo.mActive;
				if (!anyRobo && !gRedEye.mActive && random(2) == 0)
				{
					setupRedEyeGraphics();
					spawnRedEye(characterX, cameraY);
					gSpawnTimer = -1;
				}
				else
				{
					setupGraphics();
					if (trySpawn(characterX, characterY, cameraX, screenWidth))
						gSpawnTimer = -1;
					else
						gSpawnTimer = 3 * 60;		// No room right now: try again in a moment
				}
			}

			for (Robo& robo : gRobos)
			{
				if (robo.mActive)
				{
					if (bossFight && robo.mState != State::LEAVE)
						robo.mState = State::LEAVE;
					updateRobo(robo, characterX, characterY, cameraX, cameraY, screenWidth, screenHeight);
				}
			}
			for (Laser& laser : gLasers)
			{
				if (laser.mActive)
					updateLaser(laser, characterX, characterY, cameraX, screenWidth);
			}
			if (gRedEye.mActive)
			{
				if (bossFight && gRedEye.mState != EyeState::LEAVE)
					gRedEye.mState = EyeState::LEAVE;
				updateRedEye(characterX, characterY, cameraX, cameraY, screenWidth);
			}
		}

		void onPostFrame()
		{
			bool any = false;
			for (const Robo& robo : gRobos)
				any = any || robo.mActive;
			for (const Laser& laser : gLasers)
				any = any || laser.mActive;
			if (gRedEye.mActive && gRedEyeReady)
				drawRedEye();
			if (!any || !gPaletteReady)
				return;

			EmulatorInterface& emulatorInterface = emu();
			const int cameraX = emulatorInterface.readMemory16(0xffffee80);
			const int cameraY = emulatorInterface.readMemory16(0xffffee84);
			SpriteManager& spriteManager = RenderParts::instance().getSpriteManager();
			spriteManager.setCustomSpritePaletteOverride(&gPalette, &gPalette);

			for (const Robo& robo : gRobos)
			{
				if (!robo.mActive)
					continue;
				const int x = roundToInt(robo.mX) - cameraX;
				const int y = roundToInt(robo.mY) - cameraY;
				const bool flip = (robo.mFacing > 0);		// The ROM's Egg Robo faces left
				const int mirror = flip ? -1 : 1;
				const bool moving = (robo.mState == State::FLY_IN || robo.mState == State::LEAVE || std::abs(robo.mVelocityX) > 1.5f);
				const int legsFrame = FRAME_LEGS + (robo.mAge / 8) % 3;
				drawPart(legsFrame, x - 12 * mirror, y + 28, flip, 0x9e00);
				drawPart((moving && (robo.mAge & 2)) ? FRAME_BODY_FLAME : FRAME_BODY, x, y, flip, 0x9e01);
				drawPart(FRAME_ARM, x - 28 * mirror, y - 4, flip, 0x9e02);
				if (robo.mState == State::CHARGE && (robo.mTimer & 1))
					drawPart(FRAME_LASER, x - 39 * mirror, y - 8, flip, 0x9e03);		// The laser blinking at the gun, charging
			}
			for (const Laser& laser : gLasers)
			{
				if (laser.mActive)
					drawPart(FRAME_LASER, laser.mX - cameraX, laser.mY - cameraY, laser.mDirection > 0, 0x9e03);
			}
			spriteManager.setCustomSpritePaletteOverride(nullptr, nullptr);
		}
	}
}

#endif
