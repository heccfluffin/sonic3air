/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: the notification light (the small RGB light near the hinge).
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSNotificationLight.h"

#include <3ds.h>
#include <math.h>
#include <string.h>


namespace n3ds
{
	namespace notificationlight
	{
		namespace
		{
			enum class State { UNINITIALIZED, READY, UNAVAILABLE };
			State gState = State::UNINITIALIZED;
			bool gLit = false;

			bool initialize()
			{
				if (gState == State::UNINITIALIZED)
				{
					// The light is controlled by the MCU, through the "mcu::HWC" service (see the CIA's RSF file)
					const Result result = mcuHwcInit();
					gState = R_SUCCEEDED(result) ? State::READY : State::UNAVAILABLE;
					if (gState == State::UNAVAILABLE)
						RMX_LOG_INFO("Notification light: not available (mcu::HWC error " << rmx::hexString((uint32)result, 8) << ")");
				}
				return (gState == State::READY);
			}
		}

		void pulse()
		{
			if (!initialize())
				return;

			// 32 steps of 1/8 second each, played once: three soft breaths of purple, then dark.
			// The MCU blends between the steps ("smoothing"), so it fades instead of stepping.
			InfoLedPattern pattern;
			memset(&pattern, 0, sizeof(pattern));
			pattern.delay = 0x02;			// 2/16 = 1/8 second per step, so about 4 seconds in total
			pattern.smoothing = 0x40;
			pattern.loopDelay = 0xff;		// Only once
			pattern.blinkSpeed = 0;
			for (int i = 0; i < 32; ++i)
			{
				// Breaths over steps 0..29, dark for the last steps (so it ends off)
				float brightness = 0.0f;
				if (i < 30)
				{
					const float phase = (float)i / 10.0f * (float)M_PI;		// One breath every 10 steps
					brightness = sinf(phase) * sinf(phase);
				}
				pattern.redPattern[i]   = (u8)(brightness * 150.0f);		// Purple: red and blue, no green
				pattern.greenPattern[i] = 0;
				pattern.bluePattern[i]  = (u8)(brightness * 255.0f);
			}
			if (R_SUCCEEDED(MCUHWC_SetInfoLedPattern(&pattern)))
				gLit = true;
		}

		void flash()
		{
			if (!initialize())
				return;
			InfoLedPattern pattern;
			memset(&pattern, 0, sizeof(pattern));
			pattern.delay = 0x01;			// 1/16 second per step
			pattern.smoothing = 0x10;
			pattern.loopDelay = 0xff;		// Only once
			static const u8 CURVE[6] = { 255, 200, 110, 50, 15, 0 };		// Bright at once, then a short fade
			for (int i = 0; i < 6; ++i)
				pattern.redPattern[i] = pattern.greenPattern[i] = pattern.bluePattern[i] = CURVE[i];
			if (R_SUCCEEDED(MCUHWC_SetInfoLedPattern(&pattern)))
				gLit = true;
		}

		void off()
		{
			if (gState != State::READY || !gLit)
				return;
			InfoLedPattern pattern;
			memset(&pattern, 0, sizeof(pattern));
			pattern.loopDelay = 0xff;
			MCUHWC_SetInfoLedPattern(&pattern);
			gLit = false;
		}
	}
}

#endif
