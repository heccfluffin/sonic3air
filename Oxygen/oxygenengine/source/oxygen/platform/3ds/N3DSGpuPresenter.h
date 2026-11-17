/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: presenting frames with the PICA200 GPU (citro3d).
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once


namespace n3ds
{
	namespace gpu
	{
		// Presents a finished frame on the top screen using the GPU, instead of SDL's CPU-side rotated copy.
		//  - "pixels" are in ABGR8888 (the software drawer's native format), "pitchBytes" is the size of one line in bytes
		//  - The frame gets scaled from width x height into the screen rect at targetX, targetY with size targetWidth x targetHeight,
		//    picking exactly the same source pixel for each target pixel as the software blitter's nearest-neighbor scaling;
		//    the rest of the screen is black
		//  - Returns false if the GPU path is not available (e.g. initialization failed, or unsupported sizes);
		//    the caller should then present via SDL
		bool present(const void* pixels, int width, int height, int pitchBytes, int targetX, int targetY, int targetWidth, int targetHeight);
	}
}
