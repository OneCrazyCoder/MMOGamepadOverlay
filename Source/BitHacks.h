//-----------------------------------------------------------------------------
//	These were mostly all found around the internet as faster methods to do
//	math functions by bit twiddling. Most were found from StackOverflow or
//	http://graphics.stanford.edu/~seander/bithacks.html A few I made myself,
//	or have been porting from codebase to codebase for so long I don't know
//	where they originally came from.		- Taron Millet
//
//	It may need profiling to know if these are always faster than the obvious
//	basic implementation would be, since they have the side effect of making
//	it difficult for a compiler to know what is trying to be accomplished and
//	the compiler may have an even faster method of doing the same thing.
//-----------------------------------------------------------------------------

#pragma once

//-----------------------------------------------------------------------------
//	Global Function Declarations
//-----------------------------------------------------------------------------

u64 isqrt(u64 val);

// returns true for 0 and 1 as well!
bool isPowerOf2(double val);
bool isPowerOf2(float val);
bool isPowerOf2(int val);
u32 roundUpToPowerOf2(u32 val);
u32 roundDownToPowerOf2(u32 val);

u32 numberOfSetBits(u32 val);
u32 bitsRequired(u32 val); // aka position of most significant set bit + 1
u32 trailingZeroBits(u32 val); // aka position of least significant set bit

s32 getGCD(s32 a, s32 b); // i.e. Greatest Common Devisor
void reduceRatio(s32& a, s32& b);

// x * (n / d) but w/ overflow protection (and d==0 returns x)
// ratio does not need to be reduced first but it can improve accuracy
u32 mulByRatio(u32 x, u32 n, u32 d);
s32 mulByRatio(s32 x, u32 n, u32 d);
s32 mulByRatio(s32 x, s32 n, s32 d);

// Search is inclusive to pos, i.e. will just return pos if it matches request
// If requested bit not found, will return 'size' (next) or -1 (prev)
// Out of range pos does not cause issues (just returns 'size' or -1)
int nextSetBit(const u32* bitArray, int sizeOfArrayInBits, int pos);
int prevSetBit(const u32* bitArray, int sizeOfArrayInBits, int pos);
int nextClearBit(const u32* bitArray, int sizeOfArrayInBits, int pos);
int prevClearBit(const u32* bitArray, int sizeOfArrayInBits, int pos);


/*-----------------------------------------------------------------------------
	BitArray
	--------

	Lightweight replacement for stl bitset that exposes underlying array rather
	than overloading operators, and has methods to quickly iterate over all
	set/clear bits via something like:

		for(int i = ba.firstSetBit(); i < ba.size(); i = ba.nextSetBit(i+1))

	WARNING: Does NOT initialize bits (is a POD type), need to manually clear
	memory or use reset() to set all bits to false initially.
//---------------------------------------------------------------------------*/
template<size_t S> struct BitArray
{
	typedef BitArray<S> This;
	static const size_t kBitSize = (size_t)S;
	static const size_t kArraySize = (S + 31) / 32;
	static const u32 kLastBitMask = (S % 32) ? (1U << (S % 32)) - 1 : u32(-1);

	u32 bits[kArraySize];

	void set(); // sets ALL bits to true
	void set(size_t pos); // sets specific bit to true
	void set(size_t pos, bool val);
	void reset(); // sets ALL bits to false
	void reset(size_t pos); // sets specific bit to false
	void flip(); // flips ALL bits
	void flip(size_t pos);

	size_t size() const { return kBitSize; }
	bool test(size_t pos) const; // returns true/false state of bit at position
	size_t count() const; // counts total number of true bits
	bool any() const; // true if ANY bit is true
	bool none() const; // true only if ALL bits are false
	bool all() const; // true only if ALL bits are true

	// Operators treat the entire array as a single large scalar
	bool operator==(const This& rhs) const;
	bool operator!=(const This& rhs) const;
	This& operator|=(const This& rhs);
	This& operator&=(const This& rhs);
	This& operator^=(const This& rhs);
	This operator~() const;
	This operator|(const This& rhs) const;
	This operator&(const This& rhs) const;
	This operator^(const This& rhs) const;
	// Default compiler-provided assignment and copy operators work fine

	// These use int instead of size_t or unsigned int because -1 has meaning
	int firstSetBit() const { return nextSetBit(0); }
	int nextSetBit(int pos) const; // size() if none, pos if it == true
	int prevSetBit(int pos) const; // -1 if none, pos if it == true
	int lastSetBit() const { return prevSetBit(size()-1); }
	int firstClearBit() const { return nextClearBit(0); }
	int nextClearBit(int pos) const; // size() if none, pos if it == false
	int prevClearBit(int pos) const; // -1 if none, pos if it == false
	int lastClearBit() const { return prevClearBit(size()-1); }
};


/*-----------------------------------------------------------------------------
	BitVector
	---------

	Lightweight replacement for stl vector<bool> that exposes underlying array
	rather than overloading operators, and has methods to quickly iterate over
	all set/clear bits in the same manner as BitArray (but is not a POD type).

	Unlike vector<bool>, initial capacity is stored within the class's memory
	directly, and this initial capacity becomes wasted memory if the vector
	grows beyond that size as a new block of memory is dynamically allocated to
	use instead. Actual initial capacity is rounded up to nearest multiple of
	32 from what is specified in template, and when more is needed it only adds
	32 bits at a time.
//---------------------------------------------------------------------------*/
template<size_t kInitialCapacityInBits = 32> class BitVector
{
public:
	typedef BitVector<kInitialCapacityInBits> This;
	static const size_t kInitialCapacityInU32s =
		(kInitialCapacityInBits + 31) / 32;

	BitVector(size_t initialSizeInBits = 0);
	BitVector(const This&);
	template<size_t C2> BitVector(const BitVector<C2>&);
	template<size_t S> BitVector(const BitArray<S>&);
	BitVector& operator=(This);
	template<size_t C2> BitVector& operator=(BitVector<C2>);
	template<size_t S> BitVector& operator=(const BitArray<S>&);
	friend void swap<>(BitVector&, BitVector&);
	~BitVector();

	void clear() { clearAndResize(0); }
	void clearAndResize(size_t newSize); // sets all bits to 0
	void resize(size_t newSize); // sets any newly-added bits to 0
	void push_back(bool newBitValue = false);
	void pop_back();
	// Shrinks capacity as needed to fit current size, possibly resuming use
	// of internal bit buffer if previously had to heap alloc a larger one
	void trim();
	void shrink_to_fit() { trim(); }

	void set(); // sets ALL bits to true
	void set(size_t pos); // sets specific bit to true
	void set(size_t pos, bool val);
	void reset(); // sets ALL bits to false
	void reset(size_t pos); // sets specific bit to false
	void flip(); // flips ALL bits
	void flip(size_t pos);

	bool empty() const { return mSizeInBits == 0; }
	size_t size() const { return mSizeInBits; }
	size_t arraySize() const { return (mSizeInBits + 31) / 32; }
	size_t capacity() const { return mCapacityInU32s * 32; }

	bool test(size_t pos) const; // returns true/false state of bit at position
	size_t count() const; // counts total number of true bits
	bool any() const; // true if ANY bit is true
	bool none() const; // true only if ALL bits are false
	bool all() const; // true only if ALL bits are true

	// Operators treat the entire array as a single large scalar
	// For BitVector's of different size, both are treated as the size of the
	// larger with the extra bits after the end of the smaller being 0, except
	// for == and != which will return false/true if sizes do not match.
	template<size_t C2> bool operator==(const BitVector<C2>& rhs) const;
	template<size_t C2> bool operator!=(const BitVector<C2>& rhs) const;
	template<size_t C2> This& operator|=(const BitVector<C2>& rhs);
	template<size_t C2> This& operator&=(const BitVector<C2>& rhs);
	template<size_t C2> This& operator^=(const BitVector<C2>& rhs);
	This operator~() const;
	template<size_t C2> This operator|(const BitVector<C2>& rhs) const;
	template<size_t C2> This operator&(const BitVector<C2>& rhs) const;
	template<size_t C2> This operator^(const BitVector<C2>& rhs) const;

	// These use int instead of size_t or unsigned int because -1 has meaning
	int firstSetBit() const { return nextSetBit(0); }
	int nextSetBit(int pos) const; // size() if none, pos if it == true
	int prevSetBit(int pos) const; // -1 if none, pos if it == true
	int lastSetBit() const { return prevSetBit(size()-1); }
	int firstClearBit() const { return nextClearBit(0); }
	int nextClearBit(int pos) const; // size() if none, pos if it == false
	int prevClearBit(int pos) const; // -1 if none, pos if it == false
	int lastClearBit() const { return prevClearBit(size()-1); }

	u32* bits;

private:
	size_t mSizeInBits;
	size_t mCapacityInU32s;
	u32 mInitialBits[kInitialCapacityInU32s];
};


/*-----------------------------------------------------------------------------
	BitArray8
	---------

	Version of BitArray that uses u8's instead of u32's as its base data type,
	but does not have the extra methods for iterating over all set/clear bits.
	For cases where size is more important than speed or functionality.

	WARNING: Does NOT initialize bits (is a POD type), need to manually clear
	memory or use reset() to set all bits to false initially.
//---------------------------------------------------------------------------*/
template<size_t S> struct BitArray8
{
	typedef BitArray8<S> This;
	static const size_t kBitSize = (size_t)S;
	static const size_t kArraySize = (S + 7) / 8;
	static const u8 kLastBitMask = u8(u32((1U << (S % 8)) - 1) & 0xFF);

	u8 bits[kArraySize];

	void set(); // sets ALL bits to true
	void set(size_t pos); // sets specific bit to true
	void set(size_t pos, bool val);
	void reset(); // sets ALL bits to false
	void reset(size_t pos); // sets specific bit to false
	void flip(); // flips ALL bits
	void flip(size_t pos);

	size_t size() const { return kBitSize; }
	bool test(size_t pos) const; // returns true/false state of bit at position
	size_t count() const; // counts total number of true bits
	bool any() const; // true if ANY bit is true
	bool none() const; // true only if ALL bits are false
	bool all() const; // true only if ALL bits are true

	// Operators treat the entire array as a single large scalar
	bool operator==(const This& rhs) const;
	bool operator!=(const This& rhs) const;
	This& operator|=(const This& rhs);
	This& operator&=(const This& rhs);
	This& operator^=(const This& rhs);
	This operator~() const;
	This operator|(const This& rhs) const;
	This operator&(const This& rhs) const;
	This operator^(const This& rhs) const;
	// Default compiler-provided assignment and copy operators work fine
};


//-----------------------------------------------------------------------------
//	Global Function Definitions
//-----------------------------------------------------------------------------

inline u64 isqrt(u64 val)
{
	// Algorithm from "warren" and found at
	// http://stackoverflow.com/questions/1100090/looking-for-an-efficient-integer-square-root-algorithm-for-arm-thumb2

	static const u64 kDeBruijn = (~0x0218A392CD3D5DBFUL) >> 6;
	static const u64 kSqrtTable[64] = {
		0x0000000000000001L, 0x0000000001000000L, 0x0000000000000002L, 0x0000000016a09e66L,
		0x00000000016a09e6L, 0x0000000000004000L, 0x0000000000000002L, 0x000000005a827999L,
		0x0000000020000000L, 0x0000000002000000L, 0x0000000000200000L, 0x0000000000080000L,
		0x0000000000005a82L, 0x000000000000016aL, 0x0000000000000004L, 0x0000000080000000L,
		0x000000000b504f33L, 0x000000002d413cccL, 0x0000000000040000L, 0x0000000005a82799L,
		0x0000000002d413ccL, 0x00000000002d413cL, 0x0000000000000800L, 0x00000000005a8279L,
		0x00000000000b504fL, 0x0000000000016a09L, 0x0000000000008000L, 0x0000000000001000L,
		0x0000000000000200L, 0x0000000000000040L, 0x0000000000000005L, 0x00000000b504f333L,
		0x0000000000b504f3L, 0x0000000010000000L, 0x0000000000002d41L, 0x0000000040000000L,
		0x000000000016a09eL, 0x000000000005a827L, 0x0000000000000100L, 0x0000000008000000L,
		0x000000000002d413L, 0x0000000004000000L, 0x00000000000005a8L, 0x0000000000400000L,
		0x0000000000010000L, 0x0000000000000b50L, 0x000000000000002dL, 0x0000000000800000L,
		0x0000000000002000L, 0x0000000000100000L, 0x00000000000000b5L, 0x0000000000020000L,
		0x0000000000000400L, 0x000000000000b504L, 0x0000000000000020L, 0x00000000000016a0L,
		0x0000000000000080L, 0x00000000000002d4L, 0x0000000000000016L, 0x000000000000005aL,
		0x0000000000000010L, 0x000000000000000bL, 0x0000000000000008L, 0x0000000100000000L,
	};

	if( val == 0 )
		return val;

	u64 v = val;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v |= v >> 32;
	u64 y = kSqrtTable[(v * kDeBruijn) >> 58];

	y = (y + val / y) >> 1;
	y = (y + val / y) >> 1;
	y = (y + val / y) >> 1;
	y = (y + val / y) >> 1;

	return ((y * y > val) ? y-1 : y);
}


inline bool isPowerOf2(double val)
{
	int valAsInt = val;
	if( valAsInt != val )
		return false;
	return isPowerOf2(valAsInt);
}


inline bool isPowerOf2(float val)
{
	int valAsInt = val;
	if( valAsInt != val )
		return false;
	return isPowerOf2(valAsInt);
}


inline bool isPowerOf2(int val)
{
	return (val & (val - 1)) == 0;
}


inline u32 roundUpToPowerOf2(u32 val)
{
	if( (val & (val - 1)) == 0 )
		return max(1, val);

	// Algorithm by Stephane Delcroix
	// http://jeffreystedfast.blogspot.com/2008/06/calculating-nearest-power-of-2.html
	u32 j, k;
	(j = val & 0xFFFF0000) || (j = val);
	(k = j & 0xFF00FF00) || (k = j);
	(j = k & 0xF0F0F0F0) || (j = k);
	(k = j & 0xCCCCCCCC) || (k = j);
	(j = k & 0xAAAAAAAA) || (j = k);
	return j << 1;
}


inline u32 roundDownToPowerOf2(u32 val)
{
	if( (val & (val - 1)) == 0 )
		return val;

	// Algorithm by Stephane Delcroix
	// http://jeffreystedfast.blogspot.com/2008/06/calculating-nearest-power-of-2.html
	u32 j, k;
	(j = val & 0xFFFF0000) || (j = val);
	(k = j & 0xFF00FF00) || (k = j);
	(j = k & 0xF0F0F0F0) || (j = k);
	(k = j & 0xCCCCCCCC) || (k = j);
	(j = k & 0xAAAAAAAA) || (j = k);
	return j;
}


inline u32 numberOfSetBits(u32 val)
{
	#if defined(__GNUC__) || defined(__clang__)
		return __builtin_popcount(val);
	#else
		// Algorithm taken from
		// http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
		val = val - ((val >> 1) & 0x55555555);
		val = (val & 0x33333333) + ((val >> 2) & 0x33333333);
		return (((val + (val >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
	#endif
}


inline u32 bitsRequired(u32 val)
{
	// NOTE: Returns 1 if sent 0!
	#ifdef _MSC_VER
		DWORD r = 0;
		_BitScanReverse(&r, val | 1);
		return r+1;
	#elif defined(__GNUC__) || defined(__clang__)
		return 32 - __builtin_clz(val | 1);
	#else
		int r = 1;
		while (val >>= 1) ++r;
		return r;
	#endif
}


inline u32 trailingZeroBits(u32 val)
{
	// NOTE: Returns 31 if sent 0!
	#ifdef _MSC_VER
		DWORD r = 0;
		_BitScanForward(&r, val | 0x80000000);
		return r;
	#elif defined(__GNUC__) || defined(__clang__)
		return __builtin_ctz(val | 0x80000000);
	#else
		// From "Dan" @ https://stackoverflow.com/a/10866821
		const int i16 = !(val & 0xffff) << 4;
		val >>= i16;
		const int i8 = !(val & 0xff) << 3;
		val >>= i8;
		const int i4 = !(val & 0xf) << 2;
		val >>= i4;
		const int i2 = !(val & 0x3) << 1;
		val >>= i2;
		const int i1 = !(val & 0x1);
		return i16 + i8 + i4 + i2 + i1;
	#endif
}


// These ratio ones I came up with somewhat myself with help from StackOverflow, particularly "chux" - Taron
// https://stackoverflow.com/questions/57300788/fast-method-to-multiply-integer-by-proper-fraction-without-floats-or-overflow
inline s32 getGCD(s32 a, s32 b)
{
	u32 u = abs(a);
	u32 v = abs(b);
	if (u == 0) return v;
	if (v == 0) return u;
	s32 sh = trailingZeroBits(u | v);
	u >>= trailingZeroBits(u);
	do {
		v >>= trailingZeroBits(v);
		if( u > v )
			swap(u, v);
		v = v - u;
	} while(v != 0);

	return u << sh;
}


inline void reduceRatio(s32& a, s32& b)
{
	s32 aGCD = getGCD(a, b);
	a /= aGCD;
	b /= aGCD;
}


inline u32 mulByRatio(u32 x, u32 n, u32 d)
{
	u32 r = x;
	if( d == 0 ) return r;

	u32 bits = bitsRequired(x);
	int bitShift = bits - 16;
	if( bitShift < 0 ) bitShift = 0;
	int sh = bitShift;
	x >>= bitShift;

	bits = bitsRequired(n);
	bitShift = bits - 16;
	if( bitShift < 0 ) bitShift = 0;
	sh += bitShift;
	n >>= bitShift;

	bits = bitsRequired(d);
	bitShift = bits - 16;
	if( bitShift < 0 ) bitShift = 0;
	sh -= bitShift;
	d >>= bitShift;

	r = (x * n + d/2) / d;
	if( sh < 0 )
		r >>= (-sh);
	else
		r <<= sh;

	return r;
}


inline s32 mulByRatio(s32 x, s32 n, s32 d)
{
	const u32 ux = abs(x);
	const u32 un = abs(n);
	const u32 ud = abs(d);
	const bool negate = ((x ^ n ^ d) & 0x80000000) != 0;
	u32 r = mulByRatio(ux, un, ud);
	return negate ? -(s32)r : (s32)r;
}


inline s32 mulByRatio(s32 x, u32 n, u32 d)
{
	const u32 ux = abs(x);
	u32 r = mulByRatio(ux, n, d);
	return x < 0 ? -(s32)r : (s32)r;
}


inline int nextSetBit(const u32* bitArray, int size, int pos)
{
	if( pos >= size ) return size;
	pos = max(pos, 0);
	int fieldID = pos / 32;
	u32 field = bitArray[fieldID] & ~((u32(1) << u32(pos % 32)) - u32(1));
	int next = !field ? 32 : trailingZeroBits(field);
	const int kBitArraySize = (size + 31) / 32;
	while(next == 32 && ++fieldID < kBitArraySize)
	{
		field = bitArray[fieldID];
		next = !field ? 32 : trailingZeroBits(field);
	}
	next += fieldID * 32;
	return next > size ? size : next;
}


inline int prevSetBit(const u32* bitArray, int size, int pos)
{
	if( pos < 0 ) return -1;
	pos = min(pos, size-1);
	int fieldID = pos / 32;
	u32 field = bitArray[fieldID] & ((u32(2) << u32(pos % 32)) - u32(1));
	int prev = !field ? -1 : bitsRequired(field)-1;
	while(prev == -1 && --fieldID >= 0)
	{
		field = bitArray[fieldID];
		prev = !field ? -1 : bitsRequired(field)-1;
	}
	prev += fieldID * 32;
	return prev;
}


inline int nextClearBit(const u32* bitArray, int size, int pos)
{
	if( pos >= size ) return size;
	pos = max(pos, 0);
	int fieldID = pos / 32;
	u32 field = ~(bitArray[fieldID] | ((u32(1) << u32(pos % 32)) - u32(1)));
	int next = !field ? 32 : trailingZeroBits(field);
	const int kBitArraySize = (size + 31) / 32;
	while(next == 32 && ++fieldID < kBitArraySize)
	{
		field = ~bitArray[fieldID];
		next = !field ? 32 : trailingZeroBits(field);
	}
	next += fieldID * 32;
	return next > size ? size : next;
}


inline int prevClearBit(const u32* bitArray, int size, int pos)
{
	if( pos < 0 ) return -1;
	pos = min(pos, size-1);
	int fieldID = pos / 32;
	u32 field = ~bitArray[fieldID] & ((u32(2) << u32(pos % 32)) - u32(1));
	int prev = !field ? -1 : bitsRequired(field)-1;
	while(prev == -1 && --fieldID >= 0)
	{
		field = ~bitArray[fieldID];
		prev = !field ? -1 : bitsRequired(field)-1;
	}
	prev += fieldID * 32;
	return prev;
}


//-----------------------------------------------------------------------------
//	BitArray Definition
//-----------------------------------------------------------------------------

template<size_t S> inline void BitArray<S>::set()
{
	for(size_t i = 0; i < kArraySize; ++i)
		bits[i] = 0xFFFFFFFF;
}


template<size_t S> inline void BitArray<S>::set(size_t pos)
{
	DBG_ASSERT(pos < kBitSize);
	bits[pos / 32] |= (u32(1) << u32(pos % 32));
}


template<size_t S> inline void BitArray<S>::set(size_t pos, bool val)
{
	DBG_ASSERT(pos < kBitSize);
	const s32 bit = val;
	const u32 mask = u32(1) << u32(pos % 32);
	u32& bitfield = bits[pos / 32];
	bitfield = (bitfield & ~mask) | (u32(-bit) & mask);
}


template<size_t S> inline void BitArray<S>::reset()
{
	for(size_t i = 0; i < kArraySize; ++i)
		bits[i] = 0;
}


template<size_t S> inline void BitArray<S>::reset(size_t pos)
{
	DBG_ASSERT(pos < kBitSize);
	bits[pos / 32] &= ~(u32(1) << u32(pos % 32));
}


template<size_t S> inline void BitArray<S>::flip()
{
	for(size_t i = 0; i < kArraySize; ++i)
		bits[i] = ~bits[i];
}


template<size_t S> inline void BitArray<S>::flip(size_t pos)
{
	DBG_ASSERT(pos < kBitSize);
	bits[pos / 32] ^= (u32(1) << u32(pos % 32));
}


template<size_t S> inline bool BitArray<S>::test(size_t pos) const
{
	return (bits[pos / 32] >> u32(pos % 32)) & u32(1);
}


template<size_t S> inline size_t BitArray<S>::count() const
{
	size_t r = 0;
	for(size_t i = 0; i < kArraySize - 1; ++i)
		r += numberOfSetBits(bits[i]);
	const u32 last = bits[kArraySize - 1] & kLastBitMask;
	r += numberOfSetBits(last);
	return r;
}


template<size_t S> inline bool BitArray<S>::any() const
{
	for(size_t i = 0; i < kArraySize - 1; ++i)
		if( bits[i] ) return true;
	const u32 last = bits[kArraySize - 1] & kLastBitMask;
	return last != 0;
}


template<size_t S> inline bool BitArray<S>::none() const
{
	return !any();
}


template<size_t S> inline bool BitArray<S>::all() const
{
	for(size_t i = 0; i < kArraySize - 1; ++i)
		if( bits[i] != 0xFFFFFFFF ) return false;
	const u32 last = bits[kArraySize - 1] | ~kLastBitMask;
	return last == 0xFFFFFFFF;
}


template<size_t S> inline bool BitArray<S>::operator==(const This& rhs) const
{
	for(size_t i = 0; i < kArraySize - 1; ++i)
		if( bits[i] != rhs.bits[i] ) return false;
	const u32 last = bits[kArraySize - 1] & kLastBitMask;
	const u32 rhsLast = rhs.bits[kArraySize - 1] & kLastBitMask;
	return last == rhsLast;
}


template<size_t S> inline bool BitArray<S>::operator!=(const This& rhs) const
{
	return !(*this == rhs);
}


template<size_t S> inline BitArray<S>&
BitArray<S>::operator|=(const This& rhs)
{
	for(size_t i = 0; i < kArraySize; ++i)
		bits[i] |= rhs.bits[i];
	return *this;
}


template<size_t S> inline BitArray<S>&
BitArray<S>::operator&=(const This& rhs)
{
	for(size_t i = 0; i < kArraySize; ++i)
		bits[i] &= rhs.bits[i];
	return *this;
}


template<size_t S> inline BitArray<S>&
BitArray<S>::operator^=(const This& rhs)
{
	for(size_t i = 0; i < kArraySize; ++i)
		bits[i] ^= rhs.bits[i];
	return *this;
}


template<size_t S> inline BitArray<S>
BitArray<S>::operator~() const
{
	This r;
	for(size_t i = 0; i < kArraySize; ++i)
		r.bits[i] = ~bits[i];
	return r;
}


template<size_t S> inline BitArray<S>
BitArray<S>::operator|(const This& rhs) const
{
	This r(*this);
	r |= rhs;
	return r;
}


template<size_t S> inline BitArray<S>
BitArray<S>::operator&(const This& rhs) const
{
	This r(*this);
	r &= rhs;
	return r;
}


template<size_t S> inline BitArray<S>
BitArray<S>::operator^(const This& rhs) const
{
	This r(*this);
	r ^= rhs;
	return r;
}


template<size_t S> inline int BitArray<S>::nextSetBit(int pos) const
{
	return ::nextSetBit(bits, int(kBitSize), pos);
}


template<size_t S> inline int BitArray<S>::prevSetBit(int pos) const
{
	return ::prevSetBit(bits, int(kBitSize), pos);
}


template<size_t S> inline int BitArray<S>::nextClearBit(int pos) const
{
	return ::nextClearBit(bits, int(kBitSize), pos);
}


template<size_t S> inline int BitArray<S>::prevClearBit(int pos) const
{
	return ::prevClearBit(bits, int(kBitSize), pos);
}


//-----------------------------------------------------------------------------
//	BitVector Definition
//-----------------------------------------------------------------------------

template<size_t C> inline BitVector<C>::BitVector(size_t theSize)
	:
	bits(&mInitialBits[0]),
	mSizeInBits(0),
	mCapacityInU32s(kInitialCapacityInU32s)
{
	DBG_CTASSERT(kInitialCapacityInU32s > 0);
	clearAndResize(theSize);
}


template<size_t C> inline BitVector<C>::BitVector(const This& rhs)
	:
	bits(&mInitialBits[0]),
	mSizeInBits(0),
	mCapacityInU32s(kInitialCapacityInU32s)
{
	DBG_CTASSERT(kInitialCapacityInU32s > 0);
	clearAndResize(rhs.size());
	for(size_t i = 0; i < rhs.arraySize(); ++i)
		bits[i] = rhs.bits[i];
}


template<size_t C> template<size_t C2> inline
BitVector<C>::BitVector(const BitVector<C2>& rhs)
	:
	bits(&mInitialBits[0]),
	mSizeInBits(0),
	mCapacityInU32s(kInitialCapacityInU32s)
{
	DBG_CTASSERT(kInitialCapacityInU32s > 0);
	clearAndResize(rhs.size());
	for(size_t i = 0; i < rhs.arraySize(); ++i)
		bits[i] = rhs.bits[i];
}


template<size_t C> template<size_t S> inline
BitVector<C>::BitVector(const BitArray<S>& rhs)
	:
	bits(&mInitialBits[0]),
	mSizeInBits(0),
	mCapacityInU32s(kInitialCapacityInU32s)
{
	DBG_CTASSERT(kInitialCapacityInU32s > 0);
	clearAndResize(S);
	const size_t bitsToCpy = min(rhs.arraySize(), S);
	for(size_t i = 0; i < bitsToCpy; ++i)
		bits[i] = rhs.bits[i];
}


template<size_t C> inline
BitVector<C>& BitVector<C>::operator=(This rhs)
{
	swap(*this, rhs);
	return *this;
}


template<size_t C> template<size_t C2> inline
BitVector<C>& BitVector<C>::operator=(BitVector<C2> rhs)
{
	swap(*this, rhs);
	return *this;
}


template<size_t C> template<size_t S> inline
BitVector<C>& BitVector<C>::operator=(const BitArray<S>& rhs)
{
	BitVector<C> tmp(rhs);
	swap(*this, tmp);
	return *this;
}


template<size_t C> inline void
swap(BitVector<C>& lhs, BitVector<C>& rhs)
{
	BitVector<C> tmp(lhs);
	lhs.clearAndResize(rhs.size());
	for(size_t i = 0; i < rhs.arraySize(); ++i)
		lhs.bits[i] = rhs.bits[i];
	rhs.clearAndResize(tmp.size());
	for(size_t i = 0; i < tmp.arraySize(); ++i)
		rhs.bits[i] = tmp.bits[i];
}


template<size_t C> inline BitVector<C>::~BitVector()
{
	if( bits != &mInitialBits[0] )
		delete [] bits;
}


template<size_t C> inline void BitVector<C>::clearAndResize(size_t newSize)
{
	mSizeInBits = newSize;
	const size_t aNewArrayLength = (newSize + 31) / 32;
	if( aNewArrayLength > mCapacityInU32s )
	{
		mCapacityInU32s = aNewArrayLength;
		if( bits != &mInitialBits[0] )
			delete [] bits;
		bits = new u32[mCapacityInU32s];
	}

	memset(bits, 0, sizeof(u32) * mCapacityInU32s);
}


template<size_t C> inline void BitVector<C>::resize(size_t newSize)
{
	if( mSizeInBits == newSize )
		return;
	BitVector<C> tmp(newSize);
	const size_t bitsToCpy = min(tmp.arraySize(), this->arraySize());
	for(size_t i = 0; i < bitsToCpy; ++i)
		tmp.bits[i] = this->bits[i];
	this->clearAndResize(newSize);
	for(size_t i = 0; i < bitsToCpy; ++i)
		this->bits[i] = tmp.bits[i];
}


template<size_t C> inline void BitVector<C>::trim()
{
	if( this->bits != &mInitialBits[0] &&
		mCapacityInU32s > this->arraySize() )
	{
		mCapacityInU32s = this->arraySize();
		u32* const oldBits = this->bits;
		if( mCapacityInU32s > kInitialCapacityInU32s )
			this->bits = new u32[mCapacityInU32s];
		else
			this->bits = &mInitialBits[0];
		memcpy(this->bits, oldBits, mCapacityInU32s * sizeof(u32));
		delete [] oldBits;
	}
}


template<size_t C> inline void BitVector<C>::push_back(bool newBitValue)
{
	if( mSizeInBits++ == mCapacityInU32s * 32 )
	{
		u32* oldBits = this->bits;
		this->bits = new u32[mCapacityInU32s + 1];
		memcpy(this->bits, oldBits, mCapacityInU32s * sizeof(u32));
		this->bits[mCapacityInU32s] = 0;
		++mCapacityInU32s;
		if( oldBits != &mInitialBits[0] )
			delete [] oldBits;
	}

	set(mSizeInBits - 1, newBitValue);
}


template<size_t C> inline void BitVector<C>::pop_back()
{
	DBG_ASSERT(mSizeInBits);
	--mSizeInBits;
}


template<size_t C> inline void BitVector<C>::set()
{
	for(size_t i = 0; i <  this->arraySize(); ++i)
		bits[i] = 0xFFFFFFFF;
}


template<size_t C> inline void BitVector<C>::set(size_t pos)
{
	DBG_ASSERT(pos < mSizeInBits);
	bits[pos / 32] |= (u32(1) << u32(pos % 32));
}


template<size_t C> inline void BitVector<C>::set(size_t pos, bool val)
{
	DBG_ASSERT(pos < mSizeInBits);
	const s32 bit = val;
	const u32 mask = u32(1) << u32(pos % 32);
	u32& bitfield = bits[pos / 32];
	bitfield = (bitfield & ~mask) | (u32(-bit) & mask);
}


template<size_t C> inline void BitVector<C>::reset()
{
	for(size_t i = 0; i < this->arraySize(); ++i)
		bits[i] = 0;
}


template<size_t C> inline void BitVector<C>::reset(size_t pos)
{
	DBG_ASSERT(pos < mSizeInBits);
	bits[pos / 32] &= ~(u32(1) << u32(pos % 32));
}


template<size_t C> inline void BitVector<C>::flip()
{
	for(size_t i = 0; i < this->arraySize(); ++i)
		bits[i] = ~bits[i];
}


template<size_t C> inline void BitVector<C>::flip(size_t pos)
{
	DBG_ASSERT(pos < mSizeInBits);
	bits[pos / 32] ^= (u32(1) << u32(pos % 32));
}


template<size_t C> inline bool BitVector<C>::test(size_t pos) const
{
	return (bits[pos / 32] >> u32(pos % 32)) & u32(1);
}


template<size_t C> inline size_t BitVector<C>::count() const
{
	size_t r = 0;
	for(size_t i = 0; i < this->arraySize() - 1; ++i)
		r += numberOfSetBits(bits[i]);
	const u32 lastBitMask = (mSizeInBits % 32)
		? (u32(1) << (mSizeInBits % 32)) - 1 : u32(-1);
	const u32 last = bits[this->arraySize() - 1] & lastBitMask;
	r += numberOfSetBits(last);
	return r;
}


template<size_t C> inline bool BitVector<C>::any() const
{
	for(size_t i = 0; i < this->arraySize() - 1; ++i)
		if( bits[i] ) return true;
	const u32 lastBitMask = (mSizeInBits % 32)
		? (u32(1) << (mSizeInBits % 32)) - 1 : u32(-1);
	const u32 last = bits[this->arraySize() - 1] & lastBitMask;
	return last != 0;
}


template<size_t C> inline bool BitVector<C>::none() const
{
	return !any();
}


template<size_t C> inline bool BitVector<C>::all() const
{
	for(size_t i = 0; i < this->arraySize() - 1; ++i)
		if( bits[i] != 0xFFFFFFFF ) return false;
	const u32 lastBitMask = (mSizeInBits % 32)
		? (u32(1) << (mSizeInBits % 32)) - 1 : u32(-1);
	const u32 last = bits[this->arraySize() - 1] | ~lastBitMask;
	return last == 0xFFFFFFFF;
}


template<size_t C> template<size_t C2> inline bool
BitVector<C>::operator==(const BitVector<C2>& rhs) const
{
	if( this->size() != rhs.size() )
		return false;
	for(size_t i = 0; i < this->arraySize() - 1; ++i)
		if( bits[i] != rhs.bits[i] ) return false;
	const u32 lastBitMask = (mSizeInBits % 32)
		? (u32(1) << (mSizeInBits % 32)) - 1 : u32(-1);
	const u32 last = bits[this->arraySize() - 1] & lastBitMask;
	const u32 rhsLast = rhs.bits[this->arraySize() - 1] & lastBitMask;
	return last == rhsLast;
}


template<size_t C> template<size_t C2> inline bool
BitVector<C>::operator!=(const BitVector<C2>& rhs) const
{
	return !(*this == rhs);
}


template<size_t C> template<size_t C2> inline BitVector<C>&
BitVector<C>::operator|=(const BitVector<C2>& rhs)
{
	*this = *this | rhs;
	return *this;
}


template<size_t C> template<size_t C2> inline BitVector<C>&
BitVector<C>::operator&=(const BitVector<C2>& rhs)
{
	*this = *this & rhs;
	return *this;
}


template<size_t C> template<size_t C2> inline BitVector<C>&
BitVector<C>::operator^=(const BitVector<C2>& rhs)
{
	*this = *this ^ rhs;
	return *this;
}


template<size_t C> inline BitVector<C>
BitVector<C>::operator~() const
{
	This r;
	r.clearAndResize(this->size());
	for(size_t i = 0; i < this->arraySize(); ++i)
		r.bits[i] = ~bits[i];
	return r;
}


template<size_t C> template<size_t C2> inline BitVector<C>
BitVector<C>::operator|(const BitVector<C2>& rhs) const
{
	This r;
	r.clearAndResize(max(this->size(), rhs.size()));

	for(size_t i = 0; i < this->arraySize() - 1; ++i)
		r.bits[i] = this->bits[i];
	u32 lastBitMask = (this->size() % 32)
		? (u32(1) << (this->size() % 32)) - 1 : u32(-1);
	u32 last = this->bits[this->arraySize() - 1] & lastBitMask;
	r.bits[this->arraySize() - 1] = last;

	for(size_t i = 0; i < rhs.arraySize() - 1; ++i)
		r.bits[i] |= rhs.bits[i];
	lastBitMask = (rhs.size() % 32)
		? (u32(1) << (rhs.size() % 32)) - 1 : u32(-1);
	last = rhs.bits[rhs.arraySize() - 1] & lastBitMask;
	r.bits[rhs.arraySize() - 1] |= last;

	return r;
}


template<size_t C> template<size_t C2> inline BitVector<C>
BitVector<C>::operator&(const BitVector<C2>& rhs) const
{
	This r;
	r.clearAndResize(max(this->size(), rhs.size()));

	for(size_t i = 0; i < this->arraySize() - 1; ++i)
		r.bits[i] = this->bits[i];
	u32 lastBitMask = (this->size() % 32)
		? (u32(1) << (this->size() % 32)) - 1 : u32(-1);
	u32 last = this->bits[this->arraySize() - 1] & lastBitMask;
	r.bits[this->arraySize() - 1] = last;

	for(size_t i = 0; i < rhs.arraySize() - 1; ++i)
		r.bits[i] &= rhs.bits[i];
	lastBitMask = (rhs.size() % 32)
		? (u32(1) << (rhs.size() % 32)) - 1 : u32(-1);
	last = rhs.bits[rhs.arraySize() - 1] & lastBitMask;
	r.bits[rhs.arraySize() - 1] &= last;

	return r;
}


template<size_t C> template<size_t C2> inline BitVector<C>
BitVector<C>::operator^(const BitVector<C2>& rhs) const
{
	This r;
	r.clearAndResize(max(this->size(), rhs.size()));

	for(size_t i = 0; i < this->arraySize() - 1; ++i)
		r.bits[i] = this->bits[i];
	u32 lastBitMask = (this->size() % 32)
		? (u32(1) << (this->size() % 32)) - 1 : u32(-1);
	u32 last = this->bits[this->arraySize() - 1] & lastBitMask;
	r.bits[this->arraySize() - 1] = last;

	for(size_t i = 0; i < rhs.arraySize() - 1; ++i)
		r.bits[i] ^= rhs.bits[i];
	lastBitMask = (rhs.size() % 32)
		? (u32(1) << (rhs.size() % 32)) - 1 : u32(-1);
	last = rhs.bits[rhs.arraySize() - 1] & lastBitMask;
	r.bits[rhs.arraySize() - 1] ^= last;

	return r;
}


template<size_t C> inline int BitVector<C>::nextSetBit(int pos) const
{
	return ::nextSetBit(bits, int(mSizeInBits), pos);
}


template<size_t C> inline int BitVector<C>::prevSetBit(int pos) const
{
	return ::prevSetBit(bits, int(mSizeInBits), pos);
}


template<size_t C> inline int BitVector<C>::nextClearBit(int pos) const
{
	return ::nextClearBit(bits, int(mSizeInBits), pos);
}


template<size_t C> inline int BitVector<C>::prevClearBit(int pos) const
{
	return ::prevClearBit(bits, int(mSizeInBits), pos);
}


//-----------------------------------------------------------------------------
//	BitArray8 Definition
//-----------------------------------------------------------------------------

template<size_t S> inline void BitArray8<S>::set()
{
	for(size_t i = 0; i < kArraySize; ++i)
		bits[i] = 0xFF;
}


template<size_t S> inline void BitArray8<S>::set(size_t pos)
{
	DBG_ASSERT(pos < kBitSize);
	bits[pos / 8] |= (u32(1) << u8(pos % 8));
}


template<size_t S> inline void BitArray8<S>::set(size_t pos, bool val)
{
	DBG_ASSERT(pos < kBitSize);
	const s8 bit = val;
	const u8 mask = u8(1) << u8(pos % 8);
	u8& bitfield = bits[pos / 8];
	bitfield = (bitfield & ~mask) | (u8(-bit) & mask);
}


template<size_t S> inline void BitArray8<S>::reset()
{
	for(size_t i = 0; i < kArraySize; ++i)
		bits[i] = 0;
}


template<size_t S> inline void BitArray8<S>::reset(size_t pos)
{
	DBG_ASSERT(pos < kBitSize);
	bits[pos / 8] &= ~(u8(1) << u8(pos % 8));
}


template<size_t S> inline void BitArray8<S>::flip()
{
	for(size_t i = 0; i < kArraySize; ++i)
		bits[i] = ~bits[i];
}


template<size_t S> inline void BitArray8<S>::flip(size_t pos)
{
	DBG_ASSERT(pos < kBitSize);
	bits[pos / 8] ^= (u8(1) << u8(pos % 8));
}


template<size_t S> inline bool BitArray8<S>::test(size_t pos) const
{
	return (bits[pos / 8] >> u8(pos % 8)) & u8(1);
}


template<size_t S> inline size_t BitArray8<S>::count() const
{
	size_t r = 0;
	for(size_t i = 0; i < kArraySize - 1; ++i)
		r += numberOfSetBits(bits[i]);
	const u32 last = bits[kArraySize - 1] & kLastBitMask;
	r += numberOfSetBits(last);
	return r;
}


template<size_t S> inline bool BitArray8<S>::any() const
{
	for(size_t i = 0; i < kArraySize - 1; ++i)
		if( bits[i] ) return true;
	const u8 last = bits[kArraySize - 1] & kLastBitMask;
	return last != 0;
}


template<size_t S> inline bool BitArray8<S>::none() const
{
	return !any();
}


template<size_t S> inline bool BitArray8<S>::all() const
{
	for(size_t i = 0; i < kArraySize - 1; ++i)
		if( bits[i] != 0xFF ) return false;
	const u8 last = bits[kArraySize - 1] | ~kLastBitMask;
	return last == 0xFF;
}


template<size_t S> inline bool BitArray8<S>::operator==(const This& rhs) const
{
	for(size_t i = 0; i < kArraySize - 1; ++i)
		if( bits[i] != rhs.bits[i] ) return false;
	const u32 last = bits[kArraySize - 1] & kLastBitMask;
	const u32 rhsLast = rhs.bits[kArraySize - 1] & kLastBitMask;
	return last == rhsLast;
}


template<size_t S> inline bool BitArray8<S>::operator!=(const This& rhs) const
{
	return !(*this == rhs);
}


template<size_t S> inline BitArray8<S>&
BitArray8<S>::operator|=(const This& rhs)
{
	for(size_t i = 0; i < kArraySize; ++i)
		bits[i] |= rhs.bits[i];
	return *this;
}


template<size_t S> inline BitArray8<S>&
BitArray8<S>::operator&=(const This& rhs)
{
	for(size_t i = 0; i < kArraySize; ++i)
		bits[i] &= rhs.bits[i];
	return *this;
}


template<size_t S> inline BitArray8<S>&
BitArray8<S>::operator^=(const This& rhs)
{
	for(size_t i = 0; i < kArraySize; ++i)
		bits[i] ^= rhs.bits[i];
	return *this;
}


template<size_t S> inline BitArray8<S>
BitArray8<S>::operator~() const
{
	This r;
	for(size_t i = 0; i < kArraySize; ++i)
		r.bits[i] = ~bits[i];
	return r;
}


template<size_t S> inline BitArray8<S>
BitArray8<S>::operator|(const This& rhs) const
{
	This r(*this);
	r |= rhs;
	return r;
}


template<size_t S> inline BitArray8<S>
BitArray8<S>::operator&(const This& rhs) const
{
	This r(*this);
	r &= rhs;
	return r;
}


template<size_t S> inline BitArray8<S>
BitArray8<S>::operator^(const This& rhs) const
{
	This r(*this);
	r ^= rhs;
	return r;
}
