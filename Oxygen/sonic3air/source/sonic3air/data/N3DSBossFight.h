/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: is a boss (or miniboss) fight going on?
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

#include "sonic3air/audio/AudioOut.h"
#include "oxygen/simulation/EmulatorInterface.h"


namespace n3ds
{
	// The game's "level.boss_encounter" flag isn't set by every boss (Angel Island's, Marble Garden's and Carnival Night's first, and
	// more don't), so a boss theme playing counts too
	inline bool isBossFight()
	{
		if (EmulatorInterface::instance().readMemory8(0xfffff7aa) != 0)		// level.boss_encounter
			return true;
		static const uint8 BOSS_MUSIC[] =
		{
			0x18, 0x19, 0x2e, 0x30,		// Miniboss, boss, miniboss again, Big Arms
			0x1f, 0xf4, 0xf5, 0xf9,		// Knuckles' theme (his fights), in all its versions
			0xf2, 0xf3, 0xf6, 0xf7		// The boss themes from Sonic 1 and 2, the miniboss theme forced to one version
		};
		const AudioPlayer& audioPlayer = AudioOut::instance().getAudioPlayer();
		for (uint8 music : BOSS_MUSIC)
		{
			if (audioPlayer.isPlayingSfxIdQuietly(music))
				return true;
		}
		return false;
	}
}

#endif
