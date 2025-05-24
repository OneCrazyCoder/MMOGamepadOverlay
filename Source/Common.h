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
// Use template func instead of macro to avoid double-processing of expressions
// (and sometimes promote unsigned to higher-bit signed for handling negatives)
template<class T> struct num_promoted { typedef int type; };
template<> struct num_promoted<u32> { typedef s64 type; };
template<> struct num_promoted<ULONG> { typedef s64 type; };
template<> struct num_promoted<u64>; // no 's128' available to promote to!
template<> struct num_promoted<s64>; // s64 only for use as promotion from u32
template<> struct num_promoted<float> { typedef double type; };
template<> struct num_promoted<double> { typedef double type; };
template<class A, class B> struct num_common_type_impl; // unknown type match
// If both types are the same then resulting type is also the same of course
template<class T> struct num_common_type_impl<T, T> { typedef T type; };
// Only need to resolve type mismatches for post-num_promoted<> types
template<> struct num_common_type_impl<s64, int> { typedef s64 type; };
template<> struct num_common_type_impl<int, s64> { typedef s64 type; };
template<> struct num_common_type_impl<double, int> { typedef double type; };
template<> struct num_common_type_impl<int, double> { typedef double type; };
// s64 is actually promoted u32 so no precision loss comparing to a double
template<> struct num_common_type_impl<double, s64> { typedef double type; };
template<> struct num_common_type_impl<s64, double> { typedef double type; };
// Gets common type between mismatched promoted types
template<class A, class B>
struct num_common_type {
    typedef typename num_promoted<A>::type A2;
    typedef typename num_promoted<B>::type B2;
    typedef typename num_common_type_impl<A2, B2>::type type;
};
// Skip promoting any types when both are the same type anyway
template<class T> struct num_common_type<T, T> { typedef T type; };

template<class A, class B> inline
typename num_common_type<A, B>::type min(A a, B b)
{
	typedef const num_common_type<A, B>::type R;
	const R ar = R(a);
	const R br = R(b);
	return ar < br ? ar : br;
}

template<class A, class B> inline
typename num_common_type<A, B>::type max(A a, B b)
{
	typedef const num_common_type<A, B>::type R;
	const R ar = R(a);
	const R br = R(b);
	return ar > br ? ar : br;
}

template<class V, class MN, class MX> inline
typename num_common_type<V, typename num_common_type<MN, MX>::type>::type
clamp(V val, MN min, MX max)
{
	typedef typename num_common_type<MN, MX>::type MinMaxType;
	typedef typename num_common_type<V, MinMaxType>::type R;
	const R vr = R(val);
	const R minr = R(min);
	const R maxr = R(max);
	if( vr < minr ) return minr;
	if( vr > maxr ) return maxr;
	return vr;
}

inline int incWrap(int val, int count)
{ return val + 1 >= count ? 0 : val + 1; }

inline int decWrap(int val, int count)
{ return val - 1 < 0 ? int(count) - 1 : val - 1; }

inline u32 u16ToRangeVal(u16 theU16, u32 theRangeMax)
{
	return theU16 * theRangeMax / 0x10000;
}

inline u16 ratioToU16(u32 theNumerator, u32 theDenominator)
{
	// Converts ratio (numerator/denominator) to [0.0, 1.0) range represented
	// by a fixed-point 16-bit unsigned integer (0x0000 = 0.0, 0xFFFF = 0.9999)
	u64 aRatio64 = theNumerator; // 64-bit to avoid multiplication overflow
	aRatio64 *= 0x10000; // scale to 16.16 fixed-point (1.0 = 0x10000)
	aRatio64 += theDenominator - 1; // Round up (ceil) instead of truncating
	aRatio64 /= theDenominator;
	return aRatio64 > 0xFFFF ? u16(0xFFFF) : u16(aRatio64); // clamp to u16
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
