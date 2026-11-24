/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the shadow ambush.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"

#if defined(PLATFORM_3DS)

#include "sonic3air/data/N3DSAmbush.h"
#include "sonic3air/audio/AudioOut.h"

#include "oxygen/application/Application.h"
#include "oxygen/platform/3ds/N3DSChallenges.h"
#include "sonic3air/menu/SharedResources.h"
#include "oxygen/drawing/Drawer.h"
#include "oxygen/simulation/CodeExec.h"
#include "oxygen/simulation/EmulatorInterface.h"
#include "oxygen/simulation/Simulation.h"

#include <3ds.h>


namespace n3ds
{
	namespace ambush
	{
		namespace
		{
			const int START_FRAMES = 60;			// Into the act (with the character under control) before the warning
			const int WARNING_FRAMES = 180;			// The warning: 3 seconds of banner and alarm, the music fading out
			const int SURVIVED_FRAMES = 240;		// The message at the end of the act
			const uint8 SFX_ALARM = 0x86;			// Launch Base's alarm
			const uint8 MUSIC_DOOMSDAY = 0x1a;
			const int NUM_OBJECT_SLOTS = 110;		// 0xffffb000 to 0xffffcfcb

			uint16 gRolledAct = 0xffff;		// The act the dice were rolled for
			bool gActive = false;
			int gFrames = 0;				// Since the act started, counting frames the character is under control
			int gWarning = -1;				// Frames into the warning, -1 before it
			bool gChasing = false;
			bool gMusicChanged = false;
			uint8 gOriginalMusic = 0;		// The act's own music, given back when it's over
			int gSurvivedFrames = 0;
			bool gHasFrameNumber = false;
			uint16 gFrameNumber = 0;

			// Runs one of the game's script functions now, between frames, with A0 set; the game's registers are put back afterwards
			void callGameScript(const char* functionName, uint32 a0)
			{
				EmulatorInterface& emulatorInterface = EmulatorInterface::instance();
				uint32 registers[16];
				for (size_t k = 0; k < 16; ++k)
					registers[k] = emulatorInterface.getRegister(k);
				emulatorInterface.getRegister(EmulatorInterface::Register::A0) = a0;
				Application::instance().getSimulation().getCodeExec().executeScriptFunction(functionName, false);
				for (size_t k = 0; k < 16; ++k)
					emulatorInterface.getRegister(k) = registers[k];
			}

			void end()
			{
				// The act's own music back, if it's still the Doomsday one
				EmulatorInterface& emulatorInterface = EmulatorInterface::instance();
				if (gMusicChanged && emulatorInterface.readMemory8(0xffffff91) == MUSIC_DOOMSDAY)		// level.default_music
					emulatorInterface.writeMemory8(0xffffff91, gOriginalMusic);
				gActive = false;
				gChasing = false;
				gMusicChanged = false;
				gWarning = -1;
				gFrames = 0;
			}

			bool rollTheDice()
			{
				const int chance = n3ds::challenges::getAmbushChance();
				if (chance <= 0)
					return false;
				static uint32 random = (uint32)svcGetSystemTick() | 1;
				random ^= random << 13;
				random ^= random >> 17;
				random ^= random << 5;
				random ^= (uint32)svcGetSystemTick();
				return (int)((random >> 4) % 100) < chance;
			}

			void drawBanner(Drawer& drawer, int width, const char* headline, const char* subline, const Color& headlineColor, const Color& bandColor, float alpha)
			{
				const int top = 58;		// Between the rings counter and the character, so both stay in view
				const Color stripeColor(std::min(bandColor.r * 1.8f, 1.0f), std::min(bandColor.g * 1.8f, 1.0f), std::min(bandColor.b * 1.8f, 1.0f), alpha);
				drawer.drawRect(Recti(0, top, width, 36), Color(0.0f, 0.0f, 0.0f, 0.75f * alpha));
				drawer.drawRect(Recti(0, top + 2, width, 32), Color(bandColor.r, bandColor.g, bandColor.b, bandColor.a * alpha));
				drawer.drawRect(Recti(0, top, width, 2), stripeColor);
				drawer.drawRect(Recti(0, top + 34, width, 2), stripeColor);
				drawer.printText(global::mSonicFontC, Recti(0, top + 3, width, 18), headline, 5, Color(headlineColor.r, headlineColor.g, headlineColor.b, alpha));
				drawer.printText(global::mOxyfontTiny, Recti(0, top + 22, width, 10), subline, 5, Color(1.0f, 1.0f, 1.0f, alpha));
			}
		}

		void onControls(bool challengeMode, bool levelRunning, uint16 zoneAct, bool allowedHere)
		{
			if (gSurvivedFrames > 0)
				--gSurvivedFrames;
			if (!challengeMode)
			{
				// Back in the menus (or another mode): the next act played is a new roll, even if it's the same one
				if (gActive)
					end();
				gRolledAct = 0xffff;
				return;
			}
			if (!levelRunning)
				return;

			EmulatorInterface& emulatorInterface = EmulatorInterface::instance();
			if (zoneAct != gRolledAct)
			{
				// A new act (losing a life doesn't count): roll the dice
				if (gActive)
					end();
				gRolledAct = zoneAct;
				gHasFrameNumber = false;
				if (allowedHere && rollTheDice())
				{
					gActive = true;
					gFrames = 0;
					gWarning = -1;
					RMX_LOG_INFO("Shadow ambush in act " << rmx::hexString(zoneAct, 4) << " (" << n3ds::challenges::getAmbushChance() << "% chance)");
				}
			}
			if (!gActive)
				return;

			// Time goes on only with new frames of the game, and while the character can be controlled
			const uint16 frameNumber = emulatorInterface.readMemory16(0xfffffe04);		// level.framecounter
			const bool newFrame = !gHasFrameNumber || frameNumber != gFrameNumber;
			gHasFrameNumber = true;
			gFrameNumber = frameNumber;
			if (newFrame && (int8)emulatorInterface.readMemory8(0xfffff7ca) == 0)		// player1.control_override
			{
				++gFrames;
				if (gWarning < 0 && gFrames >= START_FRAMES)
				{
					gWarning = 0;
					AudioOut::instance().fadeOutChannel(0, 0.5f);		// The music fades away...
				}
				else if (gWarning >= 0 && gWarning < WARNING_FRAMES)
				{
					++gWarning;
					if (gWarning % 32 == 12 && gWarning < WARNING_FRAMES - 30)
						AudioOut::instance().playAudioDirect(SFX_ALARM, AudioOut::SoundRegType::SOUND, AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_SOUND);		// ...for the alarm
					if (gWarning == WARNING_FRAMES)
						gChasing = true;
				}
			}

			if (gChasing)
			{
				// The Doomsday music as the act's music, so it also comes back after invincibility, an extra life or drowning; not
				// during boss fights
				const uint8 music = emulatorInterface.readMemory8(0xffffff91);		// level.default_music
				if (music != MUSIC_DOOMSDAY && emulatorInterface.readMemory8(0xfffff7aa) == 0)		// level.boss_encounter
				{
					gOriginalMusic = music;
					gMusicChanged = true;
					emulatorInterface.writeMemory8(0xffffff91, MUSIC_DOOMSDAY);
					callGameScript("chooseFittingMusic", 0xffffb000);
					RMX_LOG_INFO("Shadow ambush: Doomsday music (instead of " << rmx::hexString(music, 2) << ")");
				}
			}

			// No special stages or bonus stages while he's there: giant rings and the stars over a checkpoint go away
			for (int slot = 0; slot < NUM_OBJECT_SLOTS; ++slot)
			{
				const uint32 address = 0xffffb000 + slot * 0x4a;
				const uint32 updateAddress = emulatorInterface.readMemory32(address);
				const uint8 baseState = emulatorInterface.readMemory8(address + 0x05);
				const bool giantRing = (updateAddress == 0x061682 && baseState <= 0x02);		// GiantRing.BaseUpdate, before anyone jumped in
				const bool bonusStars = (updateAddress == 0x02cfa8 && baseState == 0x08);		// Checkpoint.BaseUpdate: the stars
				if (giantRing || bonusStars)
					callGameScript("UnloadObject", address);
			}
		}

		void render(Drawer& drawer, int width)
		{
			if (gActive && gWarning >= 0 && gWarning < WARNING_FRAMES)
			{
				// WARNING, flashing, fading in and out
				const float alpha = std::min(std::min((float)gWarning / 10.0f, (float)(WARNING_FRAMES - gWarning) / 15.0f), 1.0f);
				const bool flash = ((gWarning / 16) % 2) == 0;
				drawBanner(drawer, width, "WARNING", "A SHADOW IS COMING FOR YOU", flash ? Color(1.0f, 0.3f, 0.25f) : Color(1.0f, 1.0f, 1.0f),
						   Color(0.55f, 0.0f, 0.0f, flash ? 0.6f : 0.25f), alpha);
			}
			if (gSurvivedFrames > 0)
			{
				const float alpha = std::min(std::min((float)(SURVIVED_FRAMES - gSurvivedFrames) / 10.0f, (float)gSurvivedFrames / 20.0f), 1.0f);
				drawBanner(drawer, width, "YOU OUTRAN IT", "A ROBOTNIK VOUCHER FOR YOUR TROUBLE", Color(1.0f, 0.85f, 0.2f), Color(0.1f, 0.1f, 0.3f, 0.5f), alpha);
			}
		}

		bool isActive()
		{
			return gActive && gWarning >= 0;
		}

		bool isChasing()
		{
			return gActive && gChasing;
		}

		bool onActCompleted()
		{
			if (!gActive)
				return false;
			const bool wasChasing = gChasing;
			end();
			if (wasChasing)
			{
				gSurvivedFrames = SURVIVED_FRAMES;
				RMX_LOG_INFO("Shadow ambush: outran him");
			}
			return wasChasing;
		}
	}
}

#endif
