//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

#include <cassert>

extern std::wstring gErrorString;
extern std::wstring gNoticeString;
void debugPrint(const char* fmt ...);
void logNotice(const char* fmt ...);
void logError(const char* fmt ...);
void logFatalError(const char* fmt ...);
bool hadFatalError();

#ifdef NDEBUG

#define DBG_ASSERT(exp) ((void)0)
#define DBG_LOG(exp) ((void)0)
#define assert_cast static_cast

#else

#define DBG_ASSERT(exp) { assert(exp); }
#define DBG_LOG debugPrint

// Will not assert if passed-in pointer is null, only if it is wrong type!
#define assert_cast AssertCast(__FILE__, __LINE__).cast

void assertFailed(const char* file, int line);

// Used by assert_cast.
struct AssertCast
{
	AssertCast(const char* file, int line) : mFile(file), mLine(line) {}

	template<typename NewPtr, typename Old>
	NewPtr cast(Old* oldObject) const
	{
		if( oldObject == null )
			return null;
		NewPtr newObject = dynamic_cast<NewPtr>(oldObject);
		if( newObject == null )
		{
			bool skip = false;
			assert(newObject != null);
		}
		
		return newObject;
	}

	const char* mFile;
	int mLine;
};


#endif // NDEBUG

// Compile-time assert - use DBG_CTASSERT to make sure a const static condition
// is true during compiling. Does not generate any actual code when used, just
// spits out an error if condition is false.
// Of particular use is making sure enums line up with tables with something like:
// DBG_CTASSERT(LENGTH(arrayName) == eArrayEnum_Num);
#define DBG_CTASSERT( B ) \
   typedef static_assert_test<\
   sizeof(STATIC_ASSERTION_FAILURE< (bool)( B ) >)>\
   static_assert_typedef_##__COUNTER__

// These are needed for the CTASSERT macro to work:
template <bool x> struct STATIC_ASSERTION_FAILURE;
template <> struct STATIC_ASSERTION_FAILURE<true> { enum { value = 1 }; };
template<int x> struct static_assert_test{};
