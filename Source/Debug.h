//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#pragma once

#include <cassert>

extern std::wstring gErrorString;
extern std::wstring gNoticeString;
extern bool gHadFatalError;
void debugPrint(const char* fmt ...);
void logToFile(const char* fmt ...);
void logNotice(const char* fmt ...);
void logError(const char* fmt ...);
void logFatalError(const char* fmt ...);
bool hadFatalError();

#ifdef NDEBUG
#define DBG_ASSERT(exp) (static_cast<void>(exp))
#else
#define DBG_ASSERT(exp) assert(exp)
#endif

// Compile-time assert - use DBG_CTASSERT to make sure a const static condition
// is true during compiling. Does not generate any actual code when used, just
// spits out an error if condition is false.
// Of particular use is making sure enums line up with tables w/ something like:
// DBG_CTASSERT(LENGTH(arrayName) == eArrayEnum_Num);
#define DBG_CTASSERT( B ) \
   typedef static_assert_test<\
   sizeof(STATIC_ASSERTION_FAILURE< (bool)( B ) >)>\
   static_assert_typedef_##__COUNTER__

// These are needed for the CTASSERT macro to work:
template <bool x> struct STATIC_ASSERTION_FAILURE;
template <> struct STATIC_ASSERTION_FAILURE<true> { enum { value = 1 }; };
template<int x> struct static_assert_test{};
