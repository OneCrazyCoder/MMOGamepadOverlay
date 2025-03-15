//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

#undef WIN32_LEAN_AND_MEAN
#undef _WIN32_WINNT

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#define WINVER 0x0601

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <mmsystem.h>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(disable:4244)
#pragma warning(disable:4305)
#pragma warning(disable:4786)
#pragma warning(disable:4503)
#pragma warning(disable:4018)
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

template<class Val, class Min, class Max>
inline Val clamp(Val val, Min min, Max max)
{
	if( val < min ) return min;
	if( val > max ) return max;
	return val;
}

template<class Val, class Count>
inline Val incWrap(Val val, Count count)
{
	if(int(val) + 1 >= int(count))
		return Val(0);
	else
		return Val(int(val) + 1);
}

template<class Val, class Count>
inline Val decWrap(Val val, Count count)
{
	if(int(val) - 1 < 0)
		return Val(int(count) - 1);
	else
		return Val(int(val) - 1);
}

inline u32 u16ToRangeVal(u16 theU16, u32 theRangeMax)
{
	return theU16 * theRangeMax / 0x10000;
}

inline u16 ratioToU16(u32 theNumerator, u32 theDenominator)
{
	return min(0xFFFF,
		(u64(theNumerator) * 0x10000 + theDenominator - 1) /
		theDenominator);
}

#ifndef M_PI
#define M_PI (3.1415926535897932384626433832795028841971)
#endif

/*
	Base class that clears the memory of its derived class before the derived
	class constructs. Make sure this is the first class derived from or it
	will zero out any memory initialized in any other derived classes!
	One may wonder, why not just call memset in the constructor itself?
	The answer is member variables owned by the class with their own default
	constructors, which will get called before the body of the owning class's
	constructor function body, and thus you would be zeroing out the memory of
	those owned member variables AFTER they have constructed if you did that,
	which would likely break some of them (especially any stl stuff).
	Use with the syntax:
	class MyClass : private/public ConstructFromZeroInitializedMemory<MyClass>
*/
template <typename T=void> struct ConstructFromZeroInitializedMemory
{ ConstructFromZeroInitializedMemory(){ ZeroMemory(this, sizeof(T)); } };

#include "Debug.h"

#include "BitHacks.h"
#include "GlobalConstants.h"
#include "GlobalState.h"
#include "GlobalStructures.h"
#include "Lookup.h"
#include "StringUtils.h"
