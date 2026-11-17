/*
*	rmx Library
*	Copyright (C) 2008-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once


#include "rmxbase/base/Types.h"

#if defined(PLATFORM_VITA)
	#include <psp2/kernel/clib.h>
#endif


namespace rmx
{
#if defined(__3DS__)
	// Nintendo 3DS: the ARM11 faults on unaligned 64-bit and floating point loads (LDRD / VLDR), which the compiler uses
	// for a plain pointer access of e.g. int64 or double, so always go through memcpy (it compiles to alignment-safe loads)
	template<typename T>
	T readMemoryUnaligned(const void* pointer)
	{
		T val;
		__builtin_memcpy(&val, pointer, sizeof(T));
		return val;
	}

	template<typename T>
	T readMemoryUnalignedSwapped(const void* pointer)
	{
		T val;
		__builtin_memcpy(&val, pointer, sizeof(T));
		return swapBytes<T>(val);
	}
#elif !defined(__vita__)
	template<typename T>
	T readMemoryUnaligned(const void* pointer) { return *(T*)pointer; }

	template<typename T>
	T readMemoryUnalignedSwapped(const void* pointer) { return swapBytes<T>(*(T*)pointer); }
#else
	template<typename T>
	T readMemoryUnaligned(const void* pointer)
	{
		T val;
		sceClibMemcpy(&val, pointer, sizeof(T));
		return val;
	}

	template<typename T>
	T readMemoryUnalignedSwapped(const void* pointer)
	{
		T val;
		sceClibMemcpy(&val, pointer, sizeof(T));
		return swapBytes<T>(val);
	}
#endif

#if defined(__arm__) && !defined(__vita__)
	template<> uint16 readMemoryUnaligned(const void* pointer);
	template<> uint32 readMemoryUnaligned(const void* pointer);
	template<> uint64 readMemoryUnaligned(const void* pointer);

	template<> uint16 readMemoryUnalignedSwapped(const void* pointer);
	template<> uint32 readMemoryUnalignedSwapped(const void* pointer);
	template<> uint64 readMemoryUnalignedSwapped(const void* pointer);
#endif
}
