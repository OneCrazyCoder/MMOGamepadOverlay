//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

#undef WIN32_LEAN_AND_MEAN
#undef _WIN32_WINNT
#undef WINVER

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#define WINVER 0x0601

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mmsystem.h>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(disable:4018) // Signed/unsigned mismatch in comparisons
#pragma warning(disable:4100) // Unused function parameter
#pragma warning(disable:4244) // Lossy implicit conversion (double->float)
#pragma warning(disable:4305) // Truncation in constant init (double->float)
#pragma warning(disable:4389) // Signed/unsigned mismatch in compares
#pragma warning(disable:4351) // Default-init arrays in ctor (now zero-init)
#pragma warning(disable:4503) // Decorated name too long (common in templates)
#pragma warning(disable:4505) // Unreferenced local function
#pragma warning(disable:4512) // Can't make assignment operator (const members)
#pragma warning(disable:4701) // Uninitialized local variable
#pragma warning(disable:4786) // Long debug symbol names (STL-related noise)
#endif

#define null NULL

typedef unsigned char		u8;
typedef signed char			s8;
typedef unsigned short		u16;
typedef signed short		s16;
typedef unsigned int		u32;
typedef signed int			s32;
typedef unsigned __int64	u64;
typedef signed __int64		s64;

using std::swap; // so can call unqualified for copy-and-swap idiom

#undef min
#undef max
#undef MIN
#undef MAX
#define MIN(a, b) (((b) < (a)) ? (b) : (a))
#define MAX(a, b) (((a) < (b)) ? (b) : (a))
template<class T> inline
T min(T a, T b) { return MIN(a, b); }
template<class T> inline
T max(T a, T b) { return MAX(a, b); }

template<class Val, class Min, class Max>
inline Val clamp(Val val, Min min, Max max)
{
	if( val < min ) return min;
	if( val > max ) return max;
	return val;
}

inline int incWrap(int val, int count)
{ return val + 1 >= count ? 0 : val + 1; }

inline int decWrap(int val, int count)
{ return val - 1 < 0 ? int(count) - 1 : val - 1; }

inline u32 u16ToRangeVal(u16 theU16, u32 theRangeMax)
{ return theU16 * theRangeMax / 0x10000; }

inline u16 ratioToU16(u32 theNumerator, u32 theDenominator)
{
	// Converts ratio (numerator/denominator) to [0.0, 1.0) range represented
	// by a fixed-point 16-bit unsigned integer (0x0000 = 0.0, 0xFFFF = 0.9999)
	u64 aRatio64 = theNumerator; // 64-bit to avoid multiplication overflow
	aRatio64 *= 0x10000; // scale to 16.16 fixed-point (1.0 = 0x10000)
	aRatio64 += theDenominator - 1; // Round up (ceil) instead of truncating
	aRatio64 /= theDenominator;
	return u16(min<u64>(aRatio64, 0xFFFF)); // clamp to u16 range
}

#ifndef M_PI
#define M_PI (3.1415926535897932384626433832795028841971)
#endif

// Returns one of two types based on boolean parameter - useful in templates
template<bool B, typename T, typename F>
struct conditional { typedef T type; };
template<typename T, typename F>
struct conditional<false, T, F> { typedef F type; };

/*
	Base struct that clears the memory of its derived struct before the derived
	struct constructs, to avoid issue of using memset in a struct's constructor
	(to clear POD member variables) and clobbering non-POD member variables and
	other memory like the vtable in the process. To work properly, this MUST be
	the FIRST base class derived from or it will clobber the memory of other
	base classes after they have constructed!
	
	Use with the syntax:
	struct MyStruct : private ZeroInit<MyStruct>
	or
	struct ZERO_INIT(MyStruct)
*/
template <typename T=void> struct ZeroInit
{ ZeroInit(){ ZeroMemory(this, sizeof(T)); } };
#define ZERO_INIT(S) S : private ZeroInit<S>

#include "Debug.h"

#include "BitHacks.h"
#include "FileUtils.h"
#include "GlobalConstants.h"
#include "GlobalState.h"
#include "GlobalStructures.h"
#include "Lookup.h"
#include "StringUtils.h"
