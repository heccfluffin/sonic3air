/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: presenting frames with the PICA200 GPU (citro3d).
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "PlatformDefinitions.h"

#if defined(PLATFORM_3DS) && defined(N3DS_GPU_PRESENT)

#include "oxygen/platform/3ds/N3DSGpuPresenter.h"

#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <numeric>

// Compiled vertex shader (build/_3ds/gpu/quad.v.pica), embedded at build time
#include "s3air_3ds_shaders_shbin.h"


namespace n3ds
{
	namespace gpu
	{
		namespace
		{
			// Texture dimensions must be powers of two; the 400x240 frame fits into 512x256
			constexpr int TEXTURE_WIDTH = 512;
			constexpr int TEXTURE_HEIGHT = 256;
			constexpr int SCREEN_WIDTH = 400;
			constexpr int SCREEN_HEIGHT = 240;

			// Linear RGBA8 -> tiled texture, flipped vertically, as usual for loading images into textures
			constexpr u32 TEXTURE_TRANSFER_FLAGS = GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
												   GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
												   GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

			// Render target -> top screen framebuffer, which SDL set up as RGBA8
			constexpr u32 DISPLAY_TRANSFER_FLAGS = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
												   GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
												   GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

			enum class State
			{
				UNINITIALIZED,
				READY,
				FAILED
			};

			State gState = State::UNINITIALIZED;
			C3D_RenderTarget* gTarget = nullptr;
			DVLB_s* gShaderDvlb = nullptr;
			shaderProgram_s gProgram;
			int gProjectionLocation = -1;
			C3D_Mtx gProjection;
			C3D_Tex gTextures[2];		// Alternating, so a texture still in use by the GPU for the previous frame never gets overwritten
			int gCurrentTexture = 0;
			u32* gStagingBuffer = nullptr;	// In linear memory, which the GPU's DMA can read from

			bool initialize()
			{
				if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))
					return false;

				gTarget = C3D_RenderTargetCreate(SCREEN_HEIGHT, SCREEN_WIDTH, GPU_RB_RGBA8, GPU_RB_DEPTH16);	// The screen is rotated, hence height x width
				if (nullptr == gTarget)
					return false;
				C3D_RenderTargetSetOutput(gTarget, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

				gShaderDvlb = DVLB_ParseFile((u32*)s3air_3ds_shaders_shbin, (u32)s3air_3ds_shaders_shbin_size);
				if (nullptr == gShaderDvlb)
					return false;
				shaderProgramInit(&gProgram);
				shaderProgramSetVsh(&gProgram, &gShaderDvlb->DVLE[0]);
				gProjectionLocation = shaderInstanceGetUniformLocation(gProgram.vertexShader, "projection");

				for (C3D_Tex& texture : gTextures)
				{
					if (!C3D_TexInit(&texture, TEXTURE_WIDTH, TEXTURE_HEIGHT, GPU_RGBA8))
						return false;
					C3D_TexSetFilter(&texture, GPU_NEAREST, GPU_NEAREST);
					C3D_TexSetWrap(&texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
				}

				gStagingBuffer = (u32*)linearAlloc(TEXTURE_WIDTH * TEXTURE_HEIGHT * 4);
				if (nullptr == gStagingBuffer)
					return false;
				memset(gStagingBuffer, 0, TEXTURE_WIDTH * TEXTURE_HEIGHT * 4);

				// Pixel coordinates: x from left to right, y from bottom to top; "tilt" accounts for the rotated screen
				Mtx_OrthoTilt(&gProjection, 0.0f, (float)SCREEN_WIDTH, 0.0f, (float)SCREEN_HEIGHT, 0.0f, 1.0f, true);
				return true;
			}

			void setupRenderState(C3D_Tex& texture)
			{
				C3D_BindProgram(&gProgram);

				C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
				AttrInfo_Init(attrInfo);
				AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3);	// v0: position
				AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 2);	// v1: texture coordinates

				C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
				C3D_CullFace(GPU_CULL_NONE);
				C3D_AlphaTest(false, GPU_ALWAYS, 0);
				C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);	// No blending

				C3D_TexBind(0, &texture);
				C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, gProjectionLocation, &gProjection);

				// Texture combiner: the frame is in ABGR8888, i.e. its bytes are R, G, B, A in memory.
				// The GPU's RGBA8 format expects them in reverse order, so it reads the texture's channels as:
				//   texture.r = frame alpha, texture.g = frame blue, texture.b = frame green, texture.a = frame red
				// The three combiner stages put the original color back together: (texture.a, texture.b, texture.g)
				{
					// Stage 0: (texture.a, 0, 0)
					C3D_TexEnv* env = C3D_GetTexEnv(0);
					C3D_TexEnvInit(env);
					C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PRIMARY_COLOR);
					C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_ALPHA, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
					C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
					C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
					C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
					C3D_TexEnvColor(env, 0xFF0000FF);	// Format 0xAABBGGRR: red = 1, alpha = 1
				}
				{
					// Stage 1: previous + (0, texture.b, 0)
					C3D_TexEnv* env = C3D_GetTexEnv(1);
					C3D_TexEnvInit(env);
					C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
					C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_B, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
					C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
					C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
					C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
					C3D_TexEnvColor(env, 0xFF00FF00);	// Green = 1
				}
				{
					// Stage 2: previous + (0, 0, texture.g)
					C3D_TexEnv* env = C3D_GetTexEnv(2);
					C3D_TexEnvInit(env);
					C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
					C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_G, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
					C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
					C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
					C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
					C3D_TexEnvColor(env, 0xFFFF0000);	// Blue = 1
				}
				for (int stage = 3; stage < 6; ++stage)
				{
					// Pass-through for the remaining stages
					C3D_TexEnvInit(C3D_GetTexEnv(stage));
				}
			}

			void addVertex(float x, float y, float u, float v)
			{
				C3D_ImmSendAttrib(x, y, 0.5f, 0.0f);
				C3D_ImmSendAttrib(u, v, 0.0f, 0.0f);
			}
		}

		bool present(const void* pixels, int width, int height, int pitchBytes, int targetWidth, int targetHeight)
		{
			if (gState == State::UNINITIALIZED)
			{
				gState = initialize() ? State::READY : State::FAILED;
			}
			if (gState != State::READY)
				return false;
			if (width <= 0 || height <= 0 || width > TEXTURE_WIDTH || height > TEXTURE_HEIGHT || targetWidth <= 0 || targetHeight <= 0 || targetWidth > SCREEN_WIDTH || targetHeight > SCREEN_HEIGHT)
				return false;

			// Copy the frame into linear memory and let the GPU's DMA convert it into the texture layout.
			// The texture used two frames ago is not in use by the GPU anymore at this point, as starting the previous frame waited for it.
			const u8* source = (const u8*)pixels;
			for (int y = 0; y < height; ++y)
			{
				memcpy(&gStagingBuffer[y * TEXTURE_WIDTH], source + y * pitchBytes, width * 4);
			}
			GSPGPU_FlushDataCache(gStagingBuffer, TEXTURE_WIDTH * TEXTURE_HEIGHT * 4);

			gCurrentTexture = 1 - gCurrentTexture;
			C3D_Tex& texture = gTextures[gCurrentTexture];
			C3D_SyncDisplayTransfer(gStagingBuffer, GX_BUFFER_DIM(TEXTURE_WIDTH, TEXTURE_HEIGHT), (u32*)texture.data, GX_BUFFER_DIM(TEXTURE_WIDTH, TEXTURE_HEIGHT), TEXTURE_TRANSFER_FLAGS);

			// Draw the frame as one quad, 1:1 with nearest sampling, so each screen pixel gets exactly one frame pixel
			//  -> No C3D_FRAME_SYNCDRAW: that waits for the next screen refresh (i.e. V-Sync), which locks the frame rate to 30 FPS
			//     as soon as a frame takes slightly longer than 16.7 ms. Instead, only wait for the GPU to be finished with the previous frame.
			C3D_FrameBegin(0);
			C3D_RenderTargetClear(gTarget, C3D_CLEAR_ALL, 0x000000FF, 0);
			C3D_FrameDrawOn(gTarget);
			setupRenderState(texture);

			// Texture coordinates, in texels:
			//  - With the vertical flip of the texture transfer, the frame's first line ends up at texel row (TEXTURE_HEIGHT - height),
			//    and its last line at the texture's top
			//  - The software blitter's nearest-neighbor scaling picks source pixel floor(i * source / target) for target pixel i.
			//    The GPU samples at pixel centers, i.e. at (i + 0.5) * source / target + offset. With the offset below, that becomes
			//    floor(i * source / target + epsilon), where epsilon is half the smallest possible distance of i * source / target to the
			//    next integer, so the result is exactly the same - with a safety margin against GPU rounding.
			const auto getOffset = [](int source, int target)  { return -0.5f * (float)source / (float)target + (float)std::gcd(source, target) / (float)(2 * target); };
			const float offsetU = getOffset(width, targetWidth);
			const float offsetV = getOffset(height, targetHeight);

			const float u0 = offsetU / (float)TEXTURE_WIDTH;
			const float u1 = ((float)width + offsetU) / (float)TEXTURE_WIDTH;
			const float vTop    = ((float)(TEXTURE_HEIGHT - height) + offsetV) / (float)TEXTURE_HEIGHT;
			const float vBottom = ((float)TEXTURE_HEIGHT + offsetV) / (float)TEXTURE_HEIGHT;
			const float xRight  = (float)targetWidth;
			const float yTop    = (float)SCREEN_HEIGHT;
			const float yBottom = (float)(SCREEN_HEIGHT - targetHeight);

			C3D_ImmDrawBegin(GPU_TRIANGLE_STRIP);
			addVertex(0.0f,   yTop,    u0, vTop);
			addVertex(xRight, yTop,    u1, vTop);
			addVertex(0.0f,   yBottom, u0, vBottom);
			addVertex(xRight, yBottom, u1, vBottom);
			C3D_ImmDrawEnd();

			// The buffer swap for the previous frame was requested when the GPU finished it (at the latest when "C3D_FrameBegin" returned),
			// and only takes effect at the next screen refresh. Before this frame gets transferred into the framebuffer, make sure it was
			// applied, so that the buffer still on screen never gets overwritten. GSP tells whether a requested swap is still pending,
			// so this only waits when really needed - unlike V-Sync, it doesn't lock the frame rate to 60 / 30 FPS.
			while (gspIsPresentPending(GSP_SCREEN_TOP))
			{
				gspWaitForVBlank();
			}

			// GX_CMDLIST_FLUSH: only flush the command list, instead of the CPU cache for the entire linear memory (citro3d's default).
			// The only CPU-written linear memory used for drawing is the staging buffer, which got flushed above already.
			C3D_FrameEnd(GX_CMDLIST_FLUSH);
			return true;
		}
	}
}

#endif
