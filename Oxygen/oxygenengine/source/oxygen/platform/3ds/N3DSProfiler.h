/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: lightweight frame profiler.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

// Enabled only in 3DS builds configured with S3AIR_3DS_PROFILER (see build/_3ds/CMakeLists.txt).
// In all other builds, the macros below expand to nothing, so this header can be included anywhere.
//
// Every 2 seconds, average and worst-case time per frame of each section gets printed to the 3DS bottom screen
// and written to logfile.txt. Timing uses the hardware tick counter, so the overhead is a few cycles per section.

#if defined(__3DS__) && defined(N3DS_PROFILER)

#include <stdint.h>

namespace n3ds
{
	namespace profiler
	{
		enum Section
		{
			SIMULATION,		// Simulation::update: all game logic steps of this frame
			SCRIPTS,		//  - Script execution (lemonscript) inside the game logic steps
			UI_UPDATE,		// Menus / game delegate / GUI update
			AUDIO_MAIN,		// Audio update on the main thread
			RENDER,			// Application::render: everything drawn this frame, before presenting
			REFRESH,		//  - RenderParts refresh: emulated VDP state (planes, sprites, palettes) prepared for drawing
			GEOMETRY,		//  - Collecting & sorting geometries
			SW_RENDER,		//  - Software renderer: game screen
			SW_PLANES,		//    - Planes (background layers)
			SW_SPRITES,		//    - Sprites
			SW_BLUR,		//    - Blur effect
			DRAWER,			//  - Software drawer: compositing, UI, text, scaling to the window
			DRAW_TEXT,		//    - Text
			DRAW_SPRITE,	//    - Sprites (UI graphics)
			DRAW_RECT,		//    - Rects: color fills and texture copies
			DRAW_UPSCALE,	//    - Upscaled game screen
			DRAW_OTHER,		//    - Other drawing (meshes)
			STILL_BLUR,		//  - Blurring the game screen behind menus
			PRESENT_REV,	// Presenting: byte order conversion of the window surface (twice)
			PRESENT_SDL,	// Presenting: SDL_UpdateWindowSurface (rotated copy to the 3DS framebuffer)
			WAIT,			// Frame cap wait
			_NUM
		};

		void beginSection(Section section);
		void endSection(Section section);
		void addSimulationStep();
		void addWorkerTicks(uint32_t ticks);	// Thread-safe, for audio job worker threads
		uint64_t getTicks();
		void endFrame();						// Call once per frame, right after presenting

		struct Scope
		{
			inline explicit Scope(Section section) : mSection(section)	{ beginSection(section); }
			inline ~Scope()												{ endSection(mSection); }
			Section mSection;
		};

		struct WorkerScope
		{
			inline WorkerScope() : mStart(getTicks())	{}
			inline ~WorkerScope()						{ addWorkerTicks((uint32_t)(getTicks() - mStart)); }
			uint64_t mStart;
		};
	}
}

#define N3DS_PROFILE_BEGIN(section)		n3ds::profiler::beginSection(n3ds::profiler::section)
#define N3DS_PROFILE_END(section)		n3ds::profiler::endSection(n3ds::profiler::section)
#define N3DS_PROFILE_SCOPE(section)		n3ds::profiler::Scope n3dsProfilerScope(n3ds::profiler::section)
#define N3DS_PROFILE_WORKER_SCOPE()		n3ds::profiler::WorkerScope n3dsProfilerWorkerScope
#define N3DS_PROFILE_SIMULATION_STEP()	n3ds::profiler::addSimulationStep()
#define N3DS_PROFILE_END_FRAME()		n3ds::profiler::endFrame()

#else

#define N3DS_PROFILE_BEGIN(section)
#define N3DS_PROFILE_END(section)
#define N3DS_PROFILE_SCOPE(section)
#define N3DS_PROFILE_WORKER_SCOPE()
#define N3DS_PROFILE_SIMULATION_STEP()
#define N3DS_PROFILE_END_FRAME()

#endif
