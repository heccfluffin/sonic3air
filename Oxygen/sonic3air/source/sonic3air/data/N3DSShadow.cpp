/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the shadow of Doctor Robotnik's "Outrun your shadow" daily challenge.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"

#if defined(PLATFORM_3DS)

#include "sonic3air/data/N3DSShadow.h"
#include "sonic3air/audio/AudioOut.h"
#include "sonic3air/helper/GameUtils.h"

#include "oxygen/application/Application.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/rendering/parts/SpriteManager.h"
#include "oxygen/rendering/parts/palette/PaletteManager.h"
#include "oxygen/simulation/CodeExec.h"
#include "oxygen/simulation/EmulatorInterface.h"
#include "oxygen/simulation/Simulation.h"


namespace n3ds
{
	namespace shadow
	{
		namespace
		{
			const int BUFFER_SIZE = 1024;		// Recorded frames, more than the longest delay
			const int FADE_FRAMES = 30;			// To appear (and to vanish when a boss comes)
			const int REWIND_SPEED = 6;			// While knocked back, he flies back along the way five times as fast as it was run
			const int TRAIL_LENGTH = 3;			// Red afterimages...
			const int TRAIL_SPACING = 3;		// ...at the places he was this many frames ago, and twice and three times that
			const int HISTORY = TRAIL_LENGTH * TRAIL_SPACING + 1;

			// The touch box: a bit smaller than the character's own hitbox, so a touch is a real touch
			const int TOUCH_X = 14;
			const int TOUCH_Y = 20;

			// The reach of an attack: like the insta-shield making Sonic's hitbox bigger against badniks - he's mostly on the
			// ground, and the moves are done in the air
			const int ATTACK_X = 36;
			const int ATTACK_Y = 48;

			const uint8 SFX_KNOCK_BACK = 0x3d;	// The pop of a badnik

			struct Frame
			{
				uint16 mX = 0;
				uint16 mY = 0;
				uint16 mSprite = 0;			// Like the Time Attack ghosts: the animation sprite, with the drop dash and peel-out ones
				uint8  mRotation = 0;
				uint8  mFlags = 0;			// 0x01 flip x, 0x02 flip y, 0x40 drawn in front (priority)
				uint8  mCharacter = 0;		// 0 Sonic, 1 Tails, 2 Knuckles
			};

			enum class Pending
			{
				NONE,
				HURT,
				DIE
			};

			Frame gFrames[BUFFER_SIZE];
			uint32 gRecorded = 0;			// Frames recorded since the last reset
			int gDelay = START_DELAY;		// How far behind he is
			int gTargetDelay = START_DELAY;	// Where a knock-back is taking him
			bool gStarted = false;			// The delay was set for this run
			bool gWaitingForMove = true;	// He only starts following once the character set off (not while they stand at the start)
			uint16 gStartX = 0;
			uint16 gStartY = 0;
			int gFade = 0;					// 0 (invisible) to FADE_FRAMES
			int gCooldown = 0;				// No touches for a little while after one
			bool gStunned = false;			// Knocked while as far behind as he can be: flickers during the cooldown
			bool gHasFrameNumber = false;
			uint16 gFrameNumber = 0;
			uint16 gZoneAct = 0xffff;
			uint32 gHistory[HISTORY];		// The frames he was drawn at, newest first
			int gHistoryCount = 0;
			Pending gPending = Pending::NONE;
			bool gPendingFromRight = false;
			bool gTouchedNotice = false;

			PaletteBase gGrayPalettes[2];	// For above and below the water line

			inline bool isVisible()
			{
				return (gRecorded > (uint32)gDelay);
			}

			inline const Frame& getShadowFrame()
			{
				return gFrames[(gRecorded - 1 - (uint32)gDelay) % BUFFER_SIZE];
			}

			void record(EmulatorInterface& emulatorInterface)
			{
				Frame& frame = gFrames[gRecorded % BUFFER_SIZE];
				frame.mX = emulatorInterface.readMemory16(0xffffb010);			// char.position.x
				frame.mY = emulatorInterface.readMemory16(0xffffb014);			// char.position.y
				frame.mSprite = emulatorInterface.readMemory16(0x801002);		// timeattack.animSpriteEx, written for the first player every frame
				frame.mRotation = emulatorInterface.readMemory8(0xffffb026);	// char.rotation
				frame.mFlags = emulatorInterface.readMemory8(0xffffb004) & 0x03;	// char.render_flags: flip x / y
				if (emulatorInterface.readMemory16(0xffffb00a) & 0x8000)		// char.sprite_attributes: priority
					frame.mFlags |= 0x40;
				frame.mCharacter = std::min<uint8>(emulatorInterface.readMemory8(0xffffb038), 2);	// char.character
				++gRecorded;
			}

			// A gray copy of the character's colors: the brightness only, lifted a bit so he stays visible on dark backgrounds
			void updateGrayPalettes(uint8 character)
			{
				const size_t atex = 0x40 + (size_t)character * 0x20;
				const size_t size = atex + 0x20;
				static uint32 colors[0x40 + 3 * 0x20];
				for (int index = 0; index < 2; ++index)
				{
					const PaletteBase& source = RenderParts::instance().getPaletteManager().getMainPalette(index);
					PaletteBase& gray = gGrayPalettes[index];
					if (gray.getSize() != size)
						gray.initPalette(0x5ad0e5a1u + index, size, BitFlagSet<PaletteBase::Properties>(), "n3ds_shadow");

					for (size_t k = atex; k < size; ++k)
					{
						const uint32 color = source.getEntry((int)k);
						const uint32 r = color & 0xff;
						const uint32 g = (color >> 8) & 0xff;
						const uint32 b = (color >> 16) & 0xff;
						const uint32 luma = (r * 77 + g * 150 + b * 29) >> 8;
						const uint32 value = 48 + luma * 176 / 255;
						colors[k] = (color & 0xff000000) | (value << 16) | (value << 8) | value;
					}
					gray.writeRawColors(colors, size);
				}
			}

			void drawAt(EmulatorInterface& emulatorInterface, uint32 frameIndex, const Color& color, uint16 renderQueue, uint64 spriteTag)
			{
				const Frame& frame = gFrames[frameIndex % BUFFER_SIZE];
				const Frame& before = gFrames[(frameIndex + BUFFER_SIZE - 1) % BUFFER_SIZE];
				const int px = (int)frame.mX - (int)emulatorInterface.readMemory16(0xffffee80);		// camera.position.x
				const int py = (int)frame.mY - (int)emulatorInterface.readMemory16(0xffffee84);		// camera.position.y
				const Vec2i velocity((int)frame.mX - (int)before.mX, (int)frame.mY - (int)before.mY);
				const uint16 frameNumber = (uint16)frameIndex;
				s3air::drawPlayerSprite(emulatorInterface, frame.mCharacter, Vec2i(px, py), velocity, frame.mSprite, frame.mFlags, frame.mRotation, color, &frameNumber, Color::TRANSPARENT, spriteTag);
			}

			void draw(EmulatorInterface& emulatorInterface)
			{
				if (!isVisible() || gFade <= 0 || gHistoryCount == 0)
					return;
				if (gStunned && (gCooldown & 4) != 0)
					return;

				const Frame& shadow = getShadowFrame();
				updateGrayPalettes(shadow.mCharacter);
				SpriteManager& spriteManager = RenderParts::instance().getSpriteManager();
				spriteManager.setCustomSpritePaletteOverride(&gGrayPalettes[0], &gGrayPalettes[1]);

				const float visibility = (float)gFade / (float)FADE_FRAMES;

				// The red trail behind him, oldest first, where he was a few frames ago; only where he actually moved
				for (int k = TRAIL_LENGTH; k >= 1; --k)
				{
					const int index = k * TRAIL_SPACING;
					if (index >= gHistoryCount)
						continue;
					const Frame& old = gFrames[gHistory[index] % BUFFER_SIZE];
					if (std::abs((int)old.mX - (int)shadow.mX) + std::abs((int)old.mY - (int)shadow.mY) < 4)
						continue;
					const float alpha = (0.6f - 0.15f * (float)k) * visibility;
					drawAt(emulatorInterface, gHistory[index], Color(1.8f, 0.3f, 0.3f, alpha), (uint16)(0x9ef0 + TRAIL_LENGTH - k), 0x5ad0000000ull + (uint64)k * 0x10);
				}

				// Himself
				drawAt(emulatorInterface, gHistory[0], Color(1.0f, 1.0f, 1.0f, visibility), 0x9efc, 0x5ad0000000ull);
				spriteManager.setCustomSpritePaletteOverride(nullptr, nullptr);
			}

			void knockBack()
			{
				// Further behind; or, already as far behind as he can be, stunned for a moment instead (flickering, harmless)
				gTargetDelay = std::min(gDelay + KNOCK_BACK_FRAMES, MAX_DELAY);
				gCooldown = (gTargetDelay > gDelay) ? 20 : KNOCK_BACK_FRAMES;
				gStunned = (gTargetDelay <= gDelay);
				RMX_LOG_INFO("Shadow: knocked back, now " << gTargetDelay << " frames behind");
				AudioOut::instance().playAudioDirect(SFX_KNOCK_BACK, AudioOut::SoundRegType::SOUND, AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_SOUND);
			}

			void checkTouch(EmulatorInterface& emulatorInterface, bool deadly)
			{
				if (gFade < FADE_FRAMES || gDelay < gTargetDelay || gCooldown > 0 || gPending != Pending::NONE)
					return;

				const Frame& shadow = getShadowFrame();
				const int dx = (int16)(shadow.mX - emulatorInterface.readMemory16(0xffffb010));
				int dy = (int16)(shadow.mY - emulatorInterface.readMemory16(0xffffb014));
				if (emulatorInterface.readMemory16(0xffffee18) != 0)		// Level wraps vertically
				{
					const int mask = emulatorInterface.readMemory16(0xffffeeaa);
					dy &= mask;
					if (dy > mask / 2)
						dy -= mask + 1;
				}
				if (std::abs(dx) >= ATTACK_X || std::abs(dy) >= ATTACK_Y)
					return;

				// Only while the character is in their normal state and not held by an object (like a tube or a cannon)
				if (emulatorInterface.readMemory8(0xffffb005) != 0x02 || emulatorInterface.readMemory8(0xffffb02e) != 0)	// char.base_state, char.control_flags
					return;

				// Attacking him knocks him back: invincible or super, or Sonic's insta-shield or shield move, or Knuckles' glide
				// (each of these sets "char.double_jump_state" to 1 while it's going on; Tails' flight too, but that can't hit)
				const uint8 bonus = emulatorInterface.readMemory8(0xffffb02b);			// char.bonus_effect
				const uint8 character = emulatorInterface.readMemory8(0xffffb038);
				const bool inAir = (emulatorInterface.readMemory8(0xffffb02a) & 0x02) != 0;
				const bool superForm = (emulatorInterface.readMemory8(0xfffffe19) != 0);	// super.active
				const bool shieldMove = inAir && character != 1 && emulatorInterface.readMemory8(0xffffb02f) == 1;		// char.double_jump_state
				if (superForm || (bonus & 0x02) != 0 || shieldMove)
				{
					knockBack();
					return;
				}
				if (std::abs(dx) >= TOUCH_X || std::abs(dy) >= TOUCH_Y)
					return;

				// Still blinking from a hit: nothing happens
				if (emulatorInterface.readMemory8(0xffffb034) != 0)		// char.invuln.countdown
					return;

				gPending = deadly ? Pending::DIE : Pending::HURT;
				gPendingFromRight = (dx > 0);
				gTouchedNotice = true;
				gCooldown = 60;
				RMX_LOG_INFO("Shadow: touched the character" << (deadly ? " (the end)" : "") << " - dx " << dx << ", dy " << dy << ", in air " << inAir << ", double jump " << (int)emulatorInterface.readMemory8(0xffffb02f) << ", character " << (int)character << ", bonus " << (int)bonus);
			}
		}

		void reset()
		{
			gRecorded = 0;
			gDelay = START_DELAY;
			gTargetDelay = START_DELAY;
			gStarted = false;
			gWaitingForMove = true;
			gStartX = 0;
			gStartY = 0;
			gFade = 0;
			gCooldown = 0;
			gStunned = false;
			gHasFrameNumber = false;
			gHistoryCount = 0;
			gPending = Pending::NONE;
		}

		void onPostFrame(bool active, bool deadly)
		{
			EmulatorInterface& emulatorInterface = EmulatorInterface::instance();
			const bool inLevel = (emulatorInterface.readMemory8(0xfffff600) == 0x0c) && ((int8)emulatorInterface.readMemory8(0xfffff711) > 0);	// global.level_started
			if (!active || !inLevel)
			{
				if (gRecorded > 0 || gHasFrameNumber)
					reset();
				gZoneAct = 0xffff;
				return;
			}

			const uint16 zoneAct = emulatorInterface.readMemory16(0xfffffe10);
			if (zoneAct != gZoneAct)
			{
				reset();
				gZoneAct = zoneAct;
			}
			if (!gStarted)
			{
				gStarted = true;
				gDelay = gTargetDelay = deadly ? DEADLY_DELAY : START_DELAY;
			}

			// A new frame of the game, or the same one again (paused)? A jump means a save state got loaded: start over.
			const uint16 frameNumber = emulatorInterface.readMemory16(0xfffffe04);		// level.framecounter
			const bool newFrame = !gHasFrameNumber || frameNumber != gFrameNumber;
			if (gHasFrameNumber && newFrame && (uint16)(frameNumber - gFrameNumber) > 8)
			{
				reset();
				gStarted = true;
				gDelay = gTargetDelay = deadly ? DEADLY_DELAY : START_DELAY;
			}
			gHasFrameNumber = true;
			gFrameNumber = frameNumber;

			// A boss (or miniboss) fight: he fades away, and starts over once it's done
			if (emulatorInterface.readMemory8(0xfffff7aa) != 0)		// level.boss_encounter
			{
				if (newFrame && gFade > 0)
					gFade = std::max(gFade - 2, 0);
				if (gFade > 0)
				{
					draw(emulatorInterface);
				}
				else if (gRecorded > 0)
				{
					reset();
					gStarted = true;
					gDelay = gTargetDelay = deadly ? DEADLY_DELAY : START_DELAY;
				}
				return;
			}

			// In a cutscene the character can't be controlled: he waits too
			const bool cutscene = ((int8)emulatorInterface.readMemory8(0xfffff7ca) != 0);		// player1.control_override
			if (newFrame && !cutscene && gWaitingForMove)
			{
				// Where the character stands when the act (or the life) starts: following begins once they got away from there
				const uint16 x = emulatorInterface.readMemory16(0xffffb010);
				const uint16 y = emulatorInterface.readMemory16(0xffffb014);
				if (gRecorded == 0 && gStartX == 0 && gStartY == 0)
				{
					gStartX = x;
					gStartY = y;
				}
				if (std::abs((int)(int16)(x - gStartX)) + std::abs((int)(int16)(y - gStartY)) < 32)
				{
					draw(emulatorInterface);
					return;
				}
				gWaitingForMove = false;
				RMX_LOG_INFO("Shadow: follows from " << gStartX << "," << gStartY << " (frame " << frameNumber << ")");
			}
			if (newFrame && !cutscene)
			{
				record(emulatorInterface);
				if (gDelay < gTargetDelay)
					gDelay = std::min(gDelay + REWIND_SPEED, gTargetDelay);
				if (gCooldown > 0)
					--gCooldown;
				if (gCooldown == 0)
					gStunned = false;

				if (isVisible())
				{
					if (gFade == 0)
						RMX_LOG_INFO("Shadow: appears (frame " << frameNumber << ", " << gRecorded << " recorded, " << gDelay << " behind)");
					if (gFade < FADE_FRAMES)
						++gFade;
					for (int k = std::min(gHistoryCount, HISTORY - 1); k > 0; --k)
						gHistory[k] = gHistory[k - 1];
					gHistory[0] = gRecorded - 1 - (uint32)gDelay;
					gHistoryCount = std::min(gHistoryCount + 1, HISTORY);
					checkTouch(emulatorInterface, deadly);
				}
				else
				{
					// Knocked back further than the way recorded so far: out of sight until he's caught up
					gFade = 0;
					gHistoryCount = 0;
				}
			}
			draw(emulatorInterface);
		}

		void onControls()
		{
			if (gPending == Pending::NONE)
				return;
			const Pending pending = gPending;
			gPending = Pending::NONE;

			// Still in a state to get hit?
			EmulatorInterface& emulatorInterface = EmulatorInterface::instance();
			if (emulatorInterface.readMemory8(0xffffb005) != 0x02 || emulatorInterface.readMemory8(0xfffff600) != 0x0c)
				return;
			if (pending == Pending::HURT && emulatorInterface.readMemory8(0xffffb034) != 0)
				return;

			// The game's own routines, like a badnik does it (with rings they fly, without them it's a life); the registers are
			// the game's, so they're put back afterwards
			uint32 registers[16];
			for (size_t k = 0; k < 16; ++k)
				registers[k] = emulatorInterface.getRegister(k);
			emulatorInterface.getRegister(EmulatorInterface::Register::A0) = 0xffffb000;
			emulatorInterface.getRegister(EmulatorInterface::Register::A2) = 0xffffb000;		// "What hit": only its position counts, fixed below
			Application::instance().getSimulation().getCodeExec().executeScriptFunction((pending == Pending::DIE) ? "Character.Die" : "Character.GettingHurt", false);
			for (size_t k = 0; k < 16; ++k)
				emulatorInterface.getRegister(k) = registers[k];

			// Thrown away from him
			if (pending == Pending::HURT && emulatorInterface.readMemory8(0xffffb005) == 0x04)	// base_state.HURT
			{
				const int speed = std::abs((int)(int16)emulatorInterface.readMemory16(0xffffb018));		// char.velocity.x
				emulatorInterface.writeMemory16(0xffffb018, (uint16)(int16)(gPendingFromRight ? -speed : speed));
			}
		}

		bool takeTouchedNotice()
		{
			const bool notice = gTouchedNotice;
			gTouchedNotice = false;
			return notice;
		}
	}
}

#endif
