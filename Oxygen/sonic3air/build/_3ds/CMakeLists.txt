#
#	Sonic 3 A.I.R. - Nintendo 3DS homebrew build
#
#	Configure with devkitPro's 3DS toolchain, see how-to-build.md in this directory.
#	Target hardware: New 3DS / New 2DS XL (Old 3DS is not expected to reach full speed).
#

cmake_minimum_required(VERSION 3.13)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

project(Sonic3AIR_3DS C CXX)

if (NOT NINTENDO_3DS)
	message(FATAL_ERROR "This build must be configured with devkitPro's 3DS toolchain (e.g. via arm-none-eabi-cmake from /opt/devkitpro/portlibs/3ds/bin)")
endif()


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(WORKSPACE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

# Release optimization: -O2 keeps code size (and instruction cache pressure) lower than -O3 on the ARM11
set(CMAKE_C_FLAGS_RELEASE   "-O2 -g0")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g0")

add_compile_options(-Wno-psabi -Wno-unused-function -Wno-stringop-overflow -ffunction-sections -fdata-sections)
add_link_options(-Wl,--gc-sections)

add_definitions(-DENDUSER)

# Lightweight frame profiler: prints timings to the bottom screen and logfile.txt every 2 seconds
option(S3AIR_3DS_PROFILER "Enable the 3DS frame profiler" ON)
if (S3AIR_3DS_PROFILER)
	add_definitions(-DN3DS_PROFILER)
endif()

# Build id shown by the profiler: given by the CI build, otherwise the current git commit if available
if (NOT S3AIR_3DS_BUILD_ID)
	execute_process(COMMAND git rev-parse --short=7 HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE S3AIR_3DS_BUILD_ID OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if (S3AIR_3DS_BUILD_ID)
	add_definitions(-DS3AIR_3DS_BUILD_ID="${S3AIR_3DS_BUILD_ID}")
endif()



# --- SDL2 (bundled copy, includes the N3DS backend) ---

set(SDL_STATIC ON)
set(SDL_SHARED OFF)
set(SDL_TEST OFF)
add_subdirectory(${WORKSPACE_DIR}/framework/external/sdl/SDL2 SDL)
include_directories(${WORKSPACE_DIR}/framework/external/sdl/SDL2/include)
include_directories(${CMAKE_BINARY_DIR}/SDL/include)



# --- zlib (devkitPro portlib: 3ds-zlib) & minizip (bundled) ---
#  -> The bundled zlib's own CMake script renames zconf.h inside the source tree, so it's not used here

find_package(ZLIB REQUIRED)
include_directories(${WORKSPACE_DIR}/framework/external/zlib/zlib/contrib/minizip)

add_library(minizip STATIC
	${WORKSPACE_DIR}/framework/external/zlib/zlib/contrib/minizip/ioapi.c
	${WORKSPACE_DIR}/framework/external/zlib/zlib/contrib/minizip/unzip.c
	${WORKSPACE_DIR}/framework/external/zlib/zlib/contrib/minizip/zip.c)
target_link_libraries(minizip ZLIB::ZLIB)
target_compile_options(minizip PRIVATE -Wno-implicit-function-declaration -Wno-int-conversion)
target_compile_definitions(minizip PRIVATE USE_FILE32API)		# newlib has no fopen64 & co.



# --- Ogg / Vorbis (bundled) ---

include_directories(${WORKSPACE_DIR}/framework/external/ogg-vorbis/libogg/include)
include_directories(${WORKSPACE_DIR}/framework/external/ogg-vorbis/libvorbis/include)
include_directories(${WORKSPACE_DIR}/framework/external/ogg-vorbis/libvorbis/lib)

file(GLOB OGGVORBIS_SOURCES ${WORKSPACE_DIR}/framework/external/ogg-vorbis/libogg/src/*.c
							${WORKSPACE_DIR}/framework/external/ogg-vorbis/libvorbis/lib/*.c)
add_library(oggvorbis STATIC ${OGGVORBIS_SOURCES})



# --- Project include paths ---

include_directories(${WORKSPACE_DIR}/librmx/source)
include_directories(${WORKSPACE_DIR}/Oxygen/lemonscript/source)
include_directories(${WORKSPACE_DIR}/Oxygen/oxygenengine/source)
include_directories(${WORKSPACE_DIR}/Oxygen/sonic3air/source)
include_directories(${WORKSPACE_DIR}/Oxygen/sonic3air/source/external)



# --- rmxbase ---

file(GLOB_RECURSE RMXBASE_SOURCES ${WORKSPACE_DIR}/librmx/source/rmxbase/*.cpp)
add_library(rmxbase STATIC ${RMXBASE_SOURCES})
target_precompile_headers(rmxbase PRIVATE ${WORKSPACE_DIR}/librmx/source/rmxbase.h)
target_link_libraries(rmxbase ZLIB::ZLIB)



# --- rmxmedia (OpenGL parts compile to nothing on 3DS, see RMX_WITH_OPENGL_SUPPORT) ---

file(GLOB_RECURSE RMXMEDIA_SOURCES ${WORKSPACE_DIR}/librmx/source/rmxmedia/*.cpp)
add_library(rmxmedia STATIC ${RMXMEDIA_SOURCES})
target_link_libraries(rmxmedia rmxbase SDL2-static)



# --- rmxext_oggvorbis ---

file(GLOB RMXEXT_OGGVORBIS_SOURCES ${WORKSPACE_DIR}/librmx/source/rmxext_oggvorbis/*.cpp)
add_library(rmxext_oggvorbis STATIC ${RMXEXT_OGGVORBIS_SOURCES})
target_link_libraries(rmxext_oggvorbis rmxmedia oggvorbis)



# --- lemonscript ---

file(GLOB_RECURSE LEMONSCRIPT_SOURCES ${WORKSPACE_DIR}/Oxygen/lemonscript/source/lemon/*.cpp)
add_library(lemonscript STATIC ${LEMONSCRIPT_SOURCES})
target_precompile_headers(lemonscript PRIVATE ${WORKSPACE_DIR}/Oxygen/lemonscript/source/lemon/pch.h)
target_link_libraries(lemonscript rmxbase)



# --- oxygen_netcore ---

file(GLOB_RECURSE OXYGEN_NETCORE_SOURCES ${WORKSPACE_DIR}/Oxygen/oxygenengine/source/oxygen_netcore/*.cpp)
add_library(oxygen_netcore STATIC ${OXYGEN_NETCORE_SOURCES})
target_link_libraries(oxygen_netcore rmxbase)



# --- Oxygen engine ---

file(GLOB_RECURSE OXYGEN_SOURCES ${WORKSPACE_DIR}/Oxygen/oxygenengine/source/oxygen/*.cpp)
add_library(oxygen STATIC ${OXYGEN_SOURCES})
target_link_libraries(oxygen minizip rmxmedia rmxext_oggvorbis lemonscript oxygen_netcore)

# Loading screen picture (see splash/make_splash.py)
dkp_add_embedded_binary_library(s3air_3ds_splash_data ${CMAKE_CURRENT_SOURCE_DIR}/splash/s3air_splash.bin)
target_link_libraries(oxygen s3air_3ds_splash_data)

# Bottom screen: controls overview with touch buttons for save states, one artwork per theme (see bottomscreen/make_bottom_screen.py)
foreach(THEME blue crimson gold emerald night marble sea blocks city aurora sunset)
	dkp_add_embedded_binary_library(s3air_3ds_bottom_screen_${THEME}_data ${CMAKE_CURRENT_SOURCE_DIR}/bottomscreen/s3air_bottom_screen_${THEME}.bin)
	target_link_libraries(oxygen s3air_3ds_bottom_screen_${THEME}_data)
endforeach()
# For the player's own background pictures (themes folder on the SD card): overlay with transparency, and the template picture
dkp_add_embedded_binary_library(s3air_3ds_bottom_screen_overlay_data ${CMAKE_CURRENT_SOURCE_DIR}/bottomscreen/s3air_bottom_screen_overlay.bin)
dkp_add_embedded_binary_library(s3air_3ds_theme_template_data ${CMAKE_CURRENT_SOURCE_DIR}/bottomscreen/s3air_theme_template.png)
target_link_libraries(oxygen s3air_3ds_bottom_screen_overlay_data s3air_3ds_theme_template_data)
# Digits and the battery for the clock in the title bar (see bottomscreen/make_clock_glyphs.py)
dkp_add_embedded_binary_library(s3air_3ds_bottom_screen_clock_data ${CMAKE_CURRENT_SOURCE_DIR}/bottomscreen/s3air_bottom_screen_clock.bin)
target_link_libraries(oxygen s3air_3ds_bottom_screen_clock_data)

# Presenting frames with the PICA200 GPU (citro3d) instead of SDL's CPU-side rotated copy
option(S3AIR_3DS_GPU_PRESENT "Present frames via the 3DS GPU" ON)
set(S3AIR_3DS_GPU_LIBS "")
if (S3AIR_3DS_GPU_PRESENT)
	target_compile_definitions(oxygen PRIVATE N3DS_GPU_PRESENT)
	ctr_add_shader_library(s3air_3ds_shaders ${CMAKE_CURRENT_SOURCE_DIR}/gpu/quad.v.pica)
	dkp_add_embedded_binary_library(s3air_3ds_shader_data s3air_3ds_shaders)
	target_link_libraries(oxygen s3air_3ds_shader_data)
	set(S3AIR_3DS_GPU_LIBS citro3d)
endif()



# --- Sonic 3 A.I.R. executable ---

file(GLOB_RECURSE SONIC3AIR_SOURCES ${WORKSPACE_DIR}/Oxygen/sonic3air/source/sonic3air/*.cpp)
add_executable(sonic3air ${SONIC3AIR_SOURCES})
target_precompile_headers(sonic3air PRIVATE ${WORKSPACE_DIR}/Oxygen/sonic3air/source/sonic3air/pch.h)
target_link_libraries(sonic3air oxygen SDL2main SDL2-static ${S3AIR_3DS_GPU_LIBS} ctru m)

# Homebrew metadata and .3dsx packaging
ctr_generate_smdh(sonic3air.smdh
	NAME        "Sonic 3 A.I.R."
	DESCRIPTION "Angel Island Revisited - unofficial 3DS port by heccfluffin"
	AUTHOR      "heccfluffin (3DS port) / Eukaryot (original)"
	ICON        ${CMAKE_CURRENT_SOURCE_DIR}/icon.png
)
ctr_create_3dsx(sonic3air SMDH sonic3air.smdh)
