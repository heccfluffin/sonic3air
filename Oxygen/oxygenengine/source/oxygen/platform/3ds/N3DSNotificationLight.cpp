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


// The light is animated by a little thread here, with one steady color at a time, updated 20 times per second.
// Handing the MCU a whole 32-step pattern did not work out on a New 2DS XL: it always played in about 1 second, whether the
// "delay" value was 2 or 8 (libctru documents it as 1/16 seconds per step). Steady colors, timed here, don't depend on that.

namespace n3ds
{
	namespace notificationlight
	{
		namespace
		{
			enum class State { UNINITIALIZED, READY, UNAVAILABLE };
			enum class Animation { NONE, PULSE, FLASH };

			State gState = State::UNINITIALIZED;
			Thread gThread = nullptr;
			LightLock gLock;			// Guards the animation state and the writes to the light
			LightEvent gWakeEvent;
			Animation gAnimation = Animation::NONE;
			uint64_t gAnimationStart = 0;		// System ticks
			bool gLit = false;

			const constexpr double UPDATE_SECONDS = 0.05;

			// Purple breaths: each one rises and falls in BREATH_SECONDS, then stays dark for GAP_SECONDS
			const constexpr int BREATHS = 3;
			const constexpr double BREATH_SECONDS = 1.0;
			const constexpr double GAP_SECONDS = 0.35;
			const constexpr double FLASH_SECONDS = 0.3;

			void setColor(u8 red, u8 green, u8 blue)
			{
				// The same color in all steps, looping: a steady light, whatever the MCU does with the timing values
				InfoLedPattern pattern;
				memset(&pattern, 0, sizeof(pattern));
				pattern.delay = 0x10;
				for (int i = 0; i < 32; ++i)
				{
					pattern.redPattern[i] = red;
					pattern.greenPattern[i] = green;
					pattern.bluePattern[i] = blue;
				}
				MCUHWC_SetInfoLedPattern(&pattern);
				gLit = (red | green | blue) != 0;
			}

			// Brightness 0..1 of the animation at a time; "outFinished" once it's over
			float getBrightness(Animation animation, double seconds, bool& outFinished)
			{
				outFinished = false;
				if (animation == Animation::PULSE)
				{
					const double period = BREATH_SECONDS + GAP_SECONDS;
					if (seconds >= period * BREATHS)
					{
						outFinished = true;
						return 0.0f;
					}
					const double inBreath = fmod(seconds, period);
					if (inBreath >= BREATH_SECONDS)
						return 0.0f;		// The dark pause between breaths
					const float s = (float)sin(inBreath / BREATH_SECONDS * M_PI);
					return s * s;			// Soft rise and fall
				}
				if (animation == Animation::FLASH)
				{
					if (seconds >= FLASH_SECONDS)
					{
						outFinished = true;
						return 0.0f;
					}
					const float t = (float)(seconds / FLASH_SECONDS);
					return (1.0f - t) * (1.0f - t);		// Bright at once, then a quick fade
				}
				outFinished = true;
				return 0.0f;
			}

			void threadFunc(void*)
			{
				while (true)
				{
					LightEvent_Wait(&gWakeEvent);
					while (true)
					{
						LightLock_Lock(&gLock);
						const Animation animation = gAnimation;
						if (animation == Animation::NONE)
						{
							LightLock_Unlock(&gLock);
							break;
						}
						const double seconds = (double)(svcGetSystemTick() - gAnimationStart) / SYSCLOCK_ARM11;
						bool finished = false;
						const float brightness = getBrightness(animation, seconds, finished);
						if (finished)
						{
							setColor(0, 0, 0);
							gAnimation = Animation::NONE;
						}
						else if (animation == Animation::PULSE)
						{
							setColor((u8)(brightness * 150.0f), 0, (u8)(brightness * 255.0f));		// Purple: red and blue, no green
						}
						else
						{
							const u8 value = (u8)(brightness * 255.0f);
							setColor(value, value, value);
						}
						LightLock_Unlock(&gLock);
						if (finished)
							break;
						svcSleepThread((s64)(UPDATE_SECONDS * 1000000000.0));
					}
				}
			}

			bool initialize()
			{
				if (gState == State::UNINITIALIZED)
				{
					gState = State::UNAVAILABLE;

					// The light is controlled by the MCU, through the "mcu::HWC" service (see the CIA's RSF file)
					const Result result = mcuHwcInit();
					if (R_FAILED(result))
					{
						RMX_LOG_INFO("Notification light: not available (mcu::HWC error " << rmx::hexString((uint32)result, 8) << ")");
						return false;
					}

					LightLock_Init(&gLock);
					LightEvent_Init(&gWakeEvent, RESET_ONESHOT);

					// On the system core if possible, like the file writer: updating the light must never delay a game frame
					s32 mainPriority = 0x30;
					svcGetThreadPriority(&mainPriority, CUR_THREAD_HANDLE);
					const s32 priority = std::min<s32>(mainPriority + 1, 0x3f);
					gThread = threadCreate(threadFunc, nullptr, 0x2000, priority, 1, true);
					if (nullptr == gThread)
						gThread = threadCreate(threadFunc, nullptr, 0x2000, priority, -2, true);
					if (nullptr == gThread)
					{
						RMX_LOG_INFO("Notification light: could not start its thread");
						return false;
					}
					gState = State::READY;
				}
				return (gState == State::READY);
			}

			void start(Animation animation)
			{
				if (!initialize())
					return;
				LightLock_Lock(&gLock);
				gAnimation = animation;
				gAnimationStart = svcGetSystemTick();
				LightLock_Unlock(&gLock);
				LightEvent_Signal(&gWakeEvent);
			}
		}

		void pulse()
		{
			start(Animation::PULSE);
		}

		void flash()
		{
			start(Animation::FLASH);
		}

		void off()
		{
			if (gState != State::READY)
				return;
			LightLock_Lock(&gLock);
			gAnimation = Animation::NONE;
			if (gLit)
				setColor(0, 0, 0);
			LightLock_Unlock(&gLock);
		}
	}
}

#endif
