//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

#undef WIN32_LEAN_AND_MEAN
#undef _WIN32_WINNT

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0500

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
#include "StringUtils.h"
