// Host test: the few engine basics the 3DS-specific code needs, instead of the whole engine
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

namespace fake { void log(const std::string& line); }
#define RMX_LOG_INFO(x) { std::ostringstream _stream; _stream << x; fake::log(_stream.str()); }
#include <sstream>

namespace rmx
{
	std::string convertToUTF8(const std::wstring& string);
	struct UTF8Conversion
	{
		static void convertFromUTF8(const std::string& input, std::wstring& output);
	};
}

namespace FTX
{
	struct FakeFileSystem
	{
		bool createDirectory(const std::wstring& path);
		bool exists(const std::wstring& path);
		bool saveFile(const std::wstring& path, const void* data, size_t size);
	};
	extern FakeFileSystem* FileSystem;
}

// The profiler itself isn't part of the test, only the STATS button that opens it
namespace n3ds { namespace profiler { inline void showOnScreen() {} } }
