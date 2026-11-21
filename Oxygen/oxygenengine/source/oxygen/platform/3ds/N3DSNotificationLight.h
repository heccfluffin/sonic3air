/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the notification light (the small RGB light near the hinge).
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_3DS)

namespace n3ds
{
	namespace notificationlight
	{
		// Lets the light glow in soft purple pulses for a few seconds, then go dark again - e.g. for an achievement or a secret.
		// Does nothing if the light can't be used (the result gets logged once).
		void pulse();

		// One quick white blink, like a camera flash (for screenshots)
		void flash();

		// Switches it off right away, e.g. when the game exits during a pulse
		void off();
	}
}

#endif
