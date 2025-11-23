//------------------------------------------------------------------------------
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
//------------------------------------------------------------------------------

#pragma once

//------------------------------------------------------------------------------
//	Global Function Declarations
//------------------------------------------------------------------------------

int numberOfSetBits(u32 val);
int bitsRequired(u32 val); // aka position of most significant set bit + 1
int trailingZeroBits(u32 val); // aka position of least significant set bit

// Search is inclusive to pos, i.e. will just return pos if it matches request
// If requested bit not found, will return 'size' (next) or -1 (prev)
// Out of range pos does not cause issues (just returns 'size' or -1)
int nextSetBit(const u32* bitArray, int sizeOfArrayInBits, int pos);
int prevSetBit(const u32* bitArray, int sizeOfArrayInBits, int pos);
int nextClearBit(const u32* bitArray, int sizeOfArrayInBits, int pos);
int prevClearBit(const u32* bitArray, int sizeOfArrayInBits, int pos);


/*------------------------------------------------------------------------------
	BitArray
	--------

	Lightweight replacement for stl bitset that exposes underlying array rather
	than overloading operators, and has methods to quickly iterate over all
	set/clear bits via something like:

		for(int i = ba.firstSetBit(); i < ba.size(); i = ba.nextSetBit(i+1))

	WARNING: Does NOT initialize bits (is a POD type), need to manually clear
	memory or use reset() to set all bits to false initially.
//----------------------------------------------------------------------------*/
template<u32 S> struct BitArray
{
	typedef BitArray<S> This;
	static const int kSizeInBits = (int)S;
	static const int kArraySize = (S + 31U) / 32U;
	static const u32 kLastBitMask = (S % 32U) ? (1U << (S % 32U))-1U : u32(-1);

	u32 bits[kArraySize];

	void set(); // sets ALL bits to true
	void set(int pos); // sets specific bit to true
	void set(int pos, bool val);
	void reset(); // sets ALL bits to false
	void reset(int pos); // sets specific bit to false
	void flip(); // flips ALL bits
	void flip(int pos);

	int size() const { return kSizeInBits; }
	bool test(int pos) const; // returns true/false state of bit at position
	int count() const; // counts total number of true bits
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

	int firstSetBit() const { return nextSetBit(0); }
	int nextSetBit(int pos) const; // size() if none, pos if it == true
	int prevSetBit(int pos) const; // -1 if none, pos if it == true
	int lastSetBit() const { return prevSetBit(kSizeInBits-1); }
	int firstClearBit() const { return nextClearBit(0); }
	int nextClearBit(int pos) const; // size() if none, pos if it == false
	int prevClearBit(int pos) const; // -1 if none, pos if it == false
	int lastClearBit() const { return prevClearBit(kSizeInBits-1); }
};


/*------------------------------------------------------------------------------
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
//----------------------------------------------------------------------------*/
template<u32 kInitialCapacityInBits = 32> class BitVector
{
public:
	typedef BitVector<kInitialCapacityInBits> This;
	static const int kInitialCapacityInU32s =
		int(kInitialCapacityInBits + 31) / 32;

	BitVector(size_t initialSizeInBits = 0);
	BitVector(const This&);
	template<u32 C2> BitVector(const BitVector<C2>&);
	template<u32 S> BitVector(const BitArray<S>&);
	BitVector& operator=(This);
	template<u32 C2> BitVector& operator=(BitVector<C2>);
	template<u32 S> BitVector& operator=(const BitArray<S>&);
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
	void set(int pos); // sets specific bit to true
	void set(int pos, bool val);
	void reset(); // sets ALL bits to false
	void reset(int pos); // sets specific bit to false
	void flip(); // flips ALL bits
	void flip(int pos);

	bool empty() const { return mSizeInBits == 0; }
	int size() const { return mSizeInBits; }
	int arraySize() const { return max(1, (mSizeInBits + 31) / 32); }
	int capacity() const { return mCapacityInU32s * 32; }

	bool test(int pos) const; // returns true/false state of bit at position
	int count() const; // counts total number of true bits
	bool any() const; // true if ANY bit is true
	bool none() const; // true only if ALL bits are false
	bool all() const; // true only if ALL bits are true

	// Operators treat the entire array as a single large scalar
	// For BitVector's of different size, both are treated as the size of the
	// larger with the extra bits after the end of the smaller being 0, except
	// for == and != which will return false/true if sizes do not match.
	template<u32 C2> bool operator==(const BitVector<C2>& rhs) const;
	template<u32 C2> bool operator!=(const BitVector<C2>& rhs) const;
	template<u32 C2> This& operator|=(const BitVector<C2>& rhs);
	template<u32 C2> This& operator&=(const BitVector<C2>& rhs);
	template<u32 C2> This& operator^=(const BitVector<C2>& rhs);
	This operator~() const;
	template<u32 C2> This operator|(const BitVector<C2>& rhs) const;
	template<u32 C2> This operator&(const BitVector<C2>& rhs) const;
	template<u32 C2> This operator^(const BitVector<C2>& rhs) const;

	int firstSetBit() const { return nextSetBit(0); }
	int nextSetBit(int pos) const; // size() if none, pos if it == true
	int prevSetBit(int pos) const; // -1 if none, pos if it == true
	int lastSetBit() const { return prevSetBit(mSizeInBits-1); }
	int firstClearBit() const { return nextClearBit(0); }
	int nextClearBit(int pos) const; // size() if none, pos if it == false
	int prevClearBit(int pos) const; // -1 if none, pos if it == false
	int lastClearBit() const { return prevClearBit(mSizeInBits-1); }

	u32* bits;

private:
	int mSizeInBits;
	int mCapacityInU32s;
	u32 mInitialBits[kInitialCapacityInU32s];
};


/*------------------------------------------------------------------------------
	BitArray8
	---------

	Version of BitArray that uses u8's instead of u32's as its base data type,
	but does not have the extra methods for iterating over all set/clear bits.
	For cases where size is more important than speed or functionality.

	WARNING: Does NOT initialize bits (is a POD type), need to manually clear
	memory or use reset() to set all bits to false initially.
//----------------------------------------------------------------------------*/
template<u32 S> struct BitArray8
{
	typedef BitArray8<S> This;
	static const int kSizeInBits = (int)S;
	static const int kArraySize = (S + 7) / 8;
	static const u8 kLastBitMask = u8(u32((1U << (S % 8U)) - 1U) & 0xFF);

	u8 bits[kArraySize];

	void set(); // sets ALL bits to true
	void set(int pos); // sets specific bit to true
	void set(int pos, bool val);
	void reset(); // sets ALL bits to false
	void reset(int pos); // sets specific bit to false
	void flip(); // flips ALL bits
	void flip(int pos);

	int size() const { return kSizeInBits; }
	bool test(int pos) const; // returns true/false state of bit at position
	int count() const; // counts total number of true bits
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


//------------------------------------------------------------------------------
//	Global Function Definitions
//------------------------------------------------------------------------------

inline int numberOfSetBits(u32 val)
{
	#if defined(__GNUC__) || defined(__clang__)
		return int(__builtin_popcount(val));
	#else
		// Algorithm taken from
		// http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
		val = val - ((val >> 1) & 0x55555555);
		val = (val & 0x33333333) + ((val >> 2) & 0x33333333);
		return int((((val + (val >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24);
	#endif
}


inline int bitsRequired(u32 val)
{
	// NOTE: Returns 1 if sent 0!
	#ifdef _MSC_VER
		DWORD r = 0;
		_BitScanReverse(&r, val | 1);
		return int(r+1);
	#elif defined(__GNUC__) || defined(__clang__)
		return int(32 - __builtin_clz(val | 1));
	#else
		int r = 1;
		while (val >>= 1) ++r;
		return r;
	#endif
}


inline int trailingZeroBits(u32 val)
{
	// NOTE: Returns 31 if sent 0!
	#ifdef _MSC_VER
		DWORD r = 0;
		_BitScanForward(&r, val | 0x80000000);
		return int(r);
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


inline int nextSetBit(const u32* bitArray, int size, int pos)
{
	if( pos >= size ) return size;
	pos = max(pos, 0);
	int fieldID = pos / 32;
	u32 field = bitArray[fieldID] & ~((1U << (u32(pos) % 32)) - 1U);
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
	u32 field = bitArray[fieldID] & ((u32(2) << (u32(pos) % 32)) - 1U);
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
	u32 field = ~(bitArray[fieldID] | ((1U << (u32(pos) % 32)) - 1U));
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
	u32 field = ~bitArray[fieldID] & ((u32(2) << (u32(pos) % 32)) - 1U);
	int prev = !field ? -1 : bitsRequired(field)-1;
	while(prev == -1 && --fieldID >= 0)
	{
		field = ~bitArray[fieldID];
		prev = !field ? -1 : bitsRequired(field)-1;
	}
	prev += fieldID * 32;
	return prev;
}


//------------------------------------------------------------------------------
//	BitArray Definition
//------------------------------------------------------------------------------

template<u32 S> inline void BitArray<S>::set()
{
	for(int i = 0; i < kArraySize; ++i)
		this->bits[i] = 0xFFFFFFFF;
}


template<u32 S> inline void BitArray<S>::set(int pos)
{
	DBG_ASSERT(u32(pos) < S);
	this->bits[u32(pos) / 32U] |= (1U << (u32(pos) % 32U));
}


template<u32 S> inline void BitArray<S>::set(int pos, bool val)
{
	DBG_ASSERT(u32(pos) < S);
	const s32 bit = val;
	const u32 mask = 1U << (u32(pos) % 32U);
	u32& bitfield = this->bits[u32(pos) / 32U];
	bitfield = (bitfield & ~mask) | (u32(-bit) & mask);
}


template<u32 S> inline void BitArray<S>::reset()
{
	for(int i = 0; i < kArraySize; ++i)
		this->bits[i] = 0;
}


template<u32 S> inline void BitArray<S>::reset(int pos)
{
	DBG_ASSERT(u32(pos) < S);
	this->bits[u32(pos) / 32U] &= ~(1U << (u32(pos) % 32U));
}


template<u32 S> inline void BitArray<S>::flip()
{
	for(int i = 0; i < kArraySize; ++i)
		this->bits[i] = ~this->bits[i];
}


template<u32 S> inline void BitArray<S>::flip(int pos)
{
	DBG_ASSERT(u32(pos) < S);
	this->bits[u32(pos) / 32U] ^= (1U << (u32(pos) % 32U));
}


template<u32 S> inline bool BitArray<S>::test(int pos) const
{
	DBG_ASSERT(u32(pos) < S);
	return (this->bits[u32(pos) / 32U] >> (u32(pos) % 32U)) & 1U;
}


template<u32 S> inline int BitArray<S>::count() const
{
	int r = 0;
	for(int i = 0; i < kArraySize - 1; ++i)
		r += numberOfSetBits(bits[i]);
	const u32 last = this->bits[kArraySize - 1] & kLastBitMask;
	r += numberOfSetBits(last);
	return r;
}


template<u32 S> inline bool BitArray<S>::any() const
{
	for(int i = 0; i < kArraySize - 1; ++i)
	{ if( this->bits[i] ) return true; }
	const u32 last = this->bits[kArraySize - 1] & kLastBitMask;
	return last != 0;
}


template<u32 S> inline bool BitArray<S>::none() const
{
	return !any();
}


template<u32 S> inline bool BitArray<S>::all() const
{
	for(int i = 0; i < kArraySize - 1; ++i)
	{ if( this->bits[i] != 0xFFFFFFFF ) return false; }
	const u32 last = this->bits[kArraySize - 1] | ~kLastBitMask;
	return last == 0xFFFFFFFF;
}


template<u32 S> inline bool BitArray<S>::operator==(const This& rhs) const
{
	for(int i = 0; i < kArraySize - 1; ++i)
	{ if( this->bits[i] != rhs.bits[i] ) return false; }
	const u32 last = this->bits[kArraySize - 1] & kLastBitMask;
	const u32 rhsLast = rhs.bits[kArraySize - 1] & kLastBitMask;
	return last == rhsLast;
}


template<u32 S> inline bool BitArray<S>::operator!=(const This& rhs) const
{
	return !(*this == rhs);
}


template<u32 S> inline BitArray<S>&
BitArray<S>::operator|=(const This& rhs)
{
	for(int i = 0; i < kArraySize; ++i)
		this->bits[i] |= rhs.bits[i];
	return *this;
}


template<u32 S> inline BitArray<S>&
BitArray<S>::operator&=(const This& rhs)
{
	for(int i = 0; i < kArraySize; ++i)
		this->bits[i] &= rhs.bits[i];
	return *this;
}


template<u32 S> inline BitArray<S>&
BitArray<S>::operator^=(const This& rhs)
{
	for(int i = 0; i < kArraySize; ++i)
		this->bits[i] ^= rhs.bits[i];
	return *this;
}


template<u32 S> inline BitArray<S>
BitArray<S>::operator~() const
{
	This r = {};
	for(int i = 0; i < kArraySize; ++i)
		r.bits[i] = ~this->bits[i];
	return r;
}


template<u32 S> inline BitArray<S>
BitArray<S>::operator|(const This& rhs) const
{
	This r(*this);
	r |= rhs;
	return r;
}


template<u32 S> inline BitArray<S>
BitArray<S>::operator&(const This& rhs) const
{
	This r(*this);
	r &= rhs;
	return r;
}


template<u32 S> inline BitArray<S>
BitArray<S>::operator^(const This& rhs) const
{
	This r(*this);
	r ^= rhs;
	return r;
}


template<u32 S> inline int BitArray<S>::nextSetBit(int pos) const
{
	return ::nextSetBit(this->bits, kSizeInBits, pos);
}


template<u32 S> inline int BitArray<S>::prevSetBit(int pos) const
{
	return ::prevSetBit(this->bits, kSizeInBits, pos);
}


template<u32 S> inline int BitArray<S>::nextClearBit(int pos) const
{
	return ::nextClearBit(this->bits, kSizeInBits, pos);
}


template<u32 S> inline int BitArray<S>::prevClearBit(int pos) const
{
	return ::prevClearBit(this->bits, kSizeInBits, pos);
}


//------------------------------------------------------------------------------
//	BitVector Definition
//------------------------------------------------------------------------------

template<u32 C> inline BitVector<C>::BitVector(size_t theSize)
	:
	bits(&mInitialBits[0]),
	mSizeInBits(0),
	mCapacityInU32s(kInitialCapacityInU32s)
{
	DBG_CTASSERT(kInitialCapacityInU32s > 0);
	clearAndResize(theSize);
}


template<u32 C> inline BitVector<C>::BitVector(const This& rhs)
	:
	bits(&mInitialBits[0]),
	mSizeInBits(0),
	mCapacityInU32s(kInitialCapacityInU32s)
{
	DBG_CTASSERT(kInitialCapacityInU32s > 0);
	clearAndResize(rhs.size());
	for(int i = 0, end = arraySize(); i < end; ++i)
		this->bits[i] = rhs.bits[i];
}


template<u32 C> template<u32 C2> inline
BitVector<C>::BitVector(const BitVector<C2>& rhs)
	:
	bits(&mInitialBits[0]),
	mSizeInBits(0),
	mCapacityInU32s(kInitialCapacityInU32s)
{
	DBG_CTASSERT(kInitialCapacityInU32s > 0);
	clearAndResize(rhs.size());
	for(int i = 0, end = arraySize(); i < end; ++i)
		this->bits[i] = rhs.bits[i];
}


template<u32 C> template<u32 S> inline
BitVector<C>::BitVector(const BitArray<S>& rhs)
	:
	bits(&mInitialBits[0]),
	mSizeInBits(0),
	mCapacityInU32s(kInitialCapacityInU32s)
{
	DBG_CTASSERT(kInitialCapacityInU32s > 0);
	clearAndResize(S);
	for(int i = 0, end = arraySize(); i < end; ++i)
		this->bits[i] = rhs.bits[i];
}


template<u32 C> inline
BitVector<C>& BitVector<C>::operator=(This rhs)
{
	swap(*this, rhs);
	return *this;
}


template<u32 C> template<u32 C2> inline
BitVector<C>& BitVector<C>::operator=(BitVector<C2> rhs)
{
	swap(*this, rhs);
	return *this;
}


template<u32 C> template<u32 S> inline
BitVector<C>& BitVector<C>::operator=(const BitArray<S>& rhs)
{
	BitVector<C> tmp(rhs);
	swap(*this, tmp);
	return *this;
}


template<u32 C> inline void
swap(BitVector<C>& lhs, BitVector<C>& rhs)
{
	BitVector<C> tmp(lhs);
	lhs.clearAndResize(rhs.size());
	for(int i = 0, end = rhs.arraySize(); i < end; ++i)
		lhs.bits[i] = rhs.bits[i];
	rhs.clearAndResize(tmp.size());
	for(int i = 0, end = tmp.arraySize(); i < end; ++i)
		rhs.bits[i] = tmp.bits[i];
}


template<u32 C> inline BitVector<C>::~BitVector()
{
	if( this->bits != &mInitialBits[0] )
		delete [] this->bits;
}


template<u32 C> inline void BitVector<C>::clearAndResize(size_t newSize)
{
	DBG_ASSERT(newSize >= 0);
	mSizeInBits = int(newSize);
	const int aNewArrayLength = int(newSize + 31) / 32;
	if( aNewArrayLength > mCapacityInU32s )
	{
		mCapacityInU32s = aNewArrayLength;
		if( this->bits != &mInitialBits[0] )
			delete [] this->bits;
		this->bits = new u32[mCapacityInU32s];
	}

	memset(this->bits, 0, sizeof(u32) * mCapacityInU32s);
}


template<u32 C> inline void BitVector<C>::resize(size_t newSize)
{
	DBG_ASSERT(newSize >= 0);
	if( mSizeInBits == int(newSize) )
		return;
	BitVector<C> tmp(newSize);
	const int kLenToCopy = min(tmp.arraySize(), arraySize());
	for(int i = 0; i < kLenToCopy; ++i)
		tmp.bits[i] = this->bits[i];
	this->clearAndResize(newSize);
	for(int i = 0; i < kLenToCopy; ++i)
		this->bits[i] = tmp.bits[i];
}


template<u32 C> inline void BitVector<C>::trim()
{
	if( this->bits != &mInitialBits[0] &&
		mCapacityInU32s > arraySize() )
	{
		mCapacityInU32s = arraySize();
		u32* const oldBits = this->bits;
		if( mCapacityInU32s > kInitialCapacityInU32s )
			this->bits = new u32[mCapacityInU32s];
		else
			this->bits = &mInitialBits[0];
		memcpy(this->bits, oldBits, mCapacityInU32s * sizeof(u32));
		delete [] oldBits;
	}
}


template<u32 C> inline void BitVector<C>::push_back(bool newBitValue)
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


template<u32 C> inline void BitVector<C>::pop_back()
{
	DBG_ASSERT(mSizeInBits > 0);
	--mSizeInBits;
}


template<u32 C> inline void BitVector<C>::set()
{
	for(int i = 0, end = arraySize(); i < end; ++i)
		this->bits[i] = 0xFFFFFFFF;
}


template<u32 C> inline void BitVector<C>::set(int pos)
{
	DBG_ASSERT(pos >= 0 && pos < mSizeInBits);
	this->bits[u32(pos) / 32U] |= (1U << (u32(pos) % 32U));
}


template<u32 C> inline void BitVector<C>::set(int pos, bool val)
{
	DBG_ASSERT(pos >= 0 && pos < mSizeInBits);
	const s32 bit = val;
	const u32 mask = 1U << (u32(pos) % 32U);
	u32& bitfield = this->bits[u32(pos) / 32U];
	bitfield = (bitfield & ~mask) | (u32(-bit) & mask);
}


template<u32 C> inline void BitVector<C>::reset()
{
	for(int i = 0, end = arraySize(); i < end; ++i)
		this->bits[i] = 0;
}


template<u32 C> inline void BitVector<C>::reset(int pos)
{
	DBG_ASSERT(pos >= 0 && pos < mSizeInBits);
	this->bits[u32(pos) / 32U] &= ~(1U << (u32(pos) % 32U));
}


template<u32 C> inline void BitVector<C>::flip()
{
	for(int i = 0, end = arraySize(); i < end; ++i)
		this->bits[i] = ~this->bits[i];
}


template<u32 C> inline void BitVector<C>::flip(int pos)
{
	DBG_ASSERT(pos >= 0 && pos < mSizeInBits);
	this->bits[u32(pos) / 32U] ^= (1U << (u32(pos) % 32U));
}


template<u32 C> inline bool BitVector<C>::test(int pos) const
{
	DBG_ASSERT(pos >= 0 && pos < mSizeInBits);
	return (this->bits[u32(pos) / 32U] >> (u32(pos) % 32U)) & 1U;
}


template<u32 C> inline int BitVector<C>::count() const
{
	int r = 0;
	for(int i = 0, end = arraySize(); i < end - 1; ++i)
		r += numberOfSetBits(this->bits[i]);
	const u32 kLastBitMask = (u32(mSizeInBits) % 32U)
		? (1U << (u32(mSizeInBits) % 32U)) - 1U : u32(-1);
	const u32 last = this->bits[arraySize() - 1] & kLastBitMask;
	r += numberOfSetBits(last);
	return r;
}


template<u32 C> inline bool BitVector<C>::any() const
{
	for(int i = 0, end = arraySize(); i < end - 1; ++i)
	{ if( this->bits[i] ) return true; }
	const u32 kLastBitMask = (u32(mSizeInBits) % 32U)
		? (1U << (u32(mSizeInBits) % 32U)) - 1U : u32(-1);
	const u32 last = this->bits[arraySize() - 1] & kLastBitMask;
	return last != 0;
}


template<u32 C> inline bool BitVector<C>::none() const
{
	return !any();
}


template<u32 C> inline bool BitVector<C>::all() const
{
	for(int i = 0, end = arraySize(); i < end - 1; ++i)
	{ if( this->bits[i] != 0xFFFFFFFF ) return false; }
	const u32 kLastBitMask = (u32(mSizeInBits) % 32U)
		? (1U << (u32(mSizeInBits) % 32U)) - 1U : u32(-1);
	const u32 last = this->bits[arraySize() - 1] | ~kLastBitMask;
	return last == 0xFFFFFFFF;
}


template<u32 C> template<u32 C2> inline bool
BitVector<C>::operator==(const BitVector<C2>& rhs) const
{
	if( size() != rhs.size() )
		return false;
	for(int i = 0, end = arraySize(); i < end - 1; ++i)
	{ if( this->bits[i] != rhs.bits[i] ) return false; }
	const u32 kLastBitMask = (u32(mSizeInBits) % 32U)
		? (1U << (u32(mSizeInBits) % 32U)) - 1U : u32(-1);
	const u32 last = this->bits[arraySize() - 1] & kLastBitMask;
	const u32 rhsLast = rhs.bits[arraySize() - 1] & kLastBitMask;
	return last == rhsLast;
}


template<u32 C> template<u32 C2> inline bool
BitVector<C>::operator!=(const BitVector<C2>& rhs) const
{
	return !(*this == rhs);
}


template<u32 C> template<u32 C2> inline BitVector<C>&
BitVector<C>::operator|=(const BitVector<C2>& rhs)
{
	*this = *this | rhs;
	return *this;
}


template<u32 C> template<u32 C2> inline BitVector<C>&
BitVector<C>::operator&=(const BitVector<C2>& rhs)
{
	*this = *this & rhs;
	return *this;
}


template<u32 C> template<u32 C2> inline BitVector<C>&
BitVector<C>::operator^=(const BitVector<C2>& rhs)
{
	*this = *this ^ rhs;
	return *this;
}


template<u32 C> inline BitVector<C>
BitVector<C>::operator~() const
{
	This r;
	r.clearAndResize(size());
	for(int i = 0, end = arraySize(); i < end; ++i)
		r.bits[i] = ~this->bits[i];
	return r;
}


template<u32 C> template<u32 C2> inline BitVector<C>
BitVector<C>::operator|(const BitVector<C2>& rhs) const
{
	This r(max(size(), rhs.size()));

	for(int i = 0, end = arraySize(); i < end - 1; ++i)
		r.bits[i] = this->bits[i];
	u32 lastBitMask = (u32(size()) % 32U)
		? (1U << (u32(size()) % 32U)) - 1U : u32(-1);
	u32 last = this->bits[arraySize() - 1] & lastBitMask;
	r.bits[arraySize() - 1] = last;

	for(int i = 0, end = rhs.arraySize(); i < end - 1; ++i)
		r.bits[i] |= rhs.bits[i];
	lastBitMask = (u32(rhs.size()) % 32U)
		? (1U << (u32(rhs.size()) % 32U)) - 1U : u32(-1);
	last = rhs.bits[rhs.arraySize() - 1] & lastBitMask;
	r.bits[rhs.arraySize() - 1] |= last;

	return r;
}


template<u32 C> template<u32 C2> inline BitVector<C>
BitVector<C>::operator&(const BitVector<C2>& rhs) const
{
	This r(max(size(), rhs.size()));

	for(int i = 0, end = arraySize(); i < end - 1; ++i)
		r.bits[i] = this->bits[i];
	u32 lastBitMask = (u32(size()) % 32U)
		? (1U << (u32(size()) % 32U)) - 1U : u32(-1);
	u32 last = this->bits[arraySize() - 1] & lastBitMask;
	r.bits[arraySize() - 1] = last;

	for(int i = 0, end = rhs.arraySize(); i < end - 1; ++i)
		r.bits[i] &= rhs.bits[i];
	lastBitMask = (u32(rhs.size()) % 32U)
		? (1U << (u32(rhs.size()) % 32U)) - 1U : u32(-1);
	last = rhs.bits[rhs.arraySize() - 1] & lastBitMask;
	r.bits[rhs.arraySize() - 1] &= last;

	return r;
}


template<u32 C> template<u32 C2> inline BitVector<C>
BitVector<C>::operator^(const BitVector<C2>& rhs) const
{
	This r(max(size(), rhs.size()));

	for(int i = 0, end = arraySize(); i < end - 1; ++i)
		r.bits[i] = this->bits[i];
	u32 lastBitMask = (u32(size()) % 32U)
		? (1U << (u32(size()) % 32U)) - 1U : u32(-1);
	u32 last = this->bits[arraySize() - 1] & lastBitMask;
	r.bits[arraySize() - 1] = last;

	for(int i = 0, end = rhs.arraySize(); i < end - 1; ++i)
		r.bits[i] ^= rhs.bits[i];
	lastBitMask = (u32(rhs.size()) % 32U)
		? (1U << (u32(rhs.size()) % 32U)) - 1U : u32(-1);
	last = rhs.bits[rhs.arraySize() - 1] & lastBitMask;
	r.bits[rhs.arraySize() - 1] ^= last;

	return r;
}


template<u32 C> inline int BitVector<C>::nextSetBit(int pos) const
{
	return ::nextSetBit(bits, mSizeInBits, pos);
}


template<u32 C> inline int BitVector<C>::prevSetBit(int pos) const
{
	return ::prevSetBit(bits, mSizeInBits, pos);
}


template<u32 C> inline int BitVector<C>::nextClearBit(int pos) const
{
	return ::nextClearBit(bits, mSizeInBits, pos);
}


template<u32 C> inline int BitVector<C>::prevClearBit(int pos) const
{
	return ::prevClearBit(bits, mSizeInBits, pos);
}


//------------------------------------------------------------------------------
//	BitArray8 Definition
//------------------------------------------------------------------------------

template<u32 S> inline void BitArray8<S>::set()
{
	for(int i = 0; i < kArraySize; ++i)
		this->bits[i] = 0xFF;
}


template<u32 S> inline void BitArray8<S>::set(int pos)
{
	DBG_ASSERT(u32(pos) < S);
	this->bits[u32(pos) / 8U] |= (1U << (u32(pos) % 8U));
}


template<u32 S> inline void BitArray8<S>::set(int pos, bool val)
{
	DBG_ASSERT(u32(pos) < S);
	const s8 bit = val;
	const u8 mask = u8(1U << (u32(pos) % 8U));
	u8& bitfield = this->bits[pos / 8];
	bitfield = (bitfield & ~mask) | (u8(-bit) & mask);
}


template<u32 S> inline void BitArray8<S>::reset()
{
	for(int i = 0; i < kArraySize; ++i)
		this->bits[i] = 0;
}


template<u32 S> inline void BitArray8<S>::reset(int pos)
{
	DBG_ASSERT(u32(pos) < S);
	this->bits[u32(pos) / 8U] &= ~u8(1U << (u32(pos) % 8U));
}


template<u32 S> inline void BitArray8<S>::flip()
{
	for(int i = 0; i < kArraySize; ++i)
		this->bits[i] = ~this->bits[i];
}


template<u32 S> inline void BitArray8<S>::flip(int pos)
{
	DBG_ASSERT(u32(pos) < S);
	this->bits[u32(pos) / 8U] ^= u8(1U << (u32(pos) % 8U));
}


template<u32 S> inline bool BitArray8<S>::test(int pos) const
{
	DBG_ASSERT(u32(pos) < S);
	return u8(this->bits[u32(pos) / 8U] >> (u32(pos) % 8U)) & u8(1);
}


template<u32 S> inline int BitArray8<S>::count() const
{
	int r = 0;
	for(int i = 0; i < kArraySize - 1; ++i)
		r += numberOfSetBits(this->bits[i]);
	const u32 last = this->bits[kArraySize - 1] & kLastBitMask;
	r += numberOfSetBits(last);
	return r;
}


template<u32 S> inline bool BitArray8<S>::any() const
{
	for(int i = 0; i < kArraySize - 1; ++i)
	{ if( this->bits[i] ) return true; }
	const u8 last = this->bits[kArraySize - 1] & kLastBitMask;
	return last != 0;
}


template<u32 S> inline bool BitArray8<S>::none() const
{
	return !any();
}


template<u32 S> inline bool BitArray8<S>::all() const
{
	for(int i = 0; i < kArraySize - 1; ++i)
	{ if( bits[i] != 0xFF ) return false; }
	const u8 last = this->bits[kArraySize - 1] | ~kLastBitMask;
	return last == 0xFF;
}


template<u32 S> inline bool BitArray8<S>::operator==(const This& rhs) const
{
	for(int i = 0; i < kArraySize - 1; ++i)
	{ if( this->bits[i] != rhs.bits[i] ) return false; }
	const u8 last = this->bits[kArraySize - 1] & kLastBitMask;
	const u8 rhsLast = rhs.bits[kArraySize - 1] & kLastBitMask;
	return last == rhsLast;
}


template<u32 S> inline bool BitArray8<S>::operator!=(const This& rhs) const
{
	return !(*this == rhs);
}


template<u32 S> inline BitArray8<S>&
BitArray8<S>::operator|=(const This& rhs)
{
	for(int i = 0; i < kArraySize; ++i)
		this->bits[i] |= rhs.bits[i];
	return *this;
}


template<u32 S> inline BitArray8<S>&
BitArray8<S>::operator&=(const This& rhs)
{
	for(int i = 0; i < kArraySize; ++i)
		this->bits[i] &= rhs.bits[i];
	return *this;
}


template<u32 S> inline BitArray8<S>&
BitArray8<S>::operator^=(const This& rhs)
{
	for(int i = 0; i < kArraySize; ++i)
		this->bits[i] ^= rhs.bits[i];
	return *this;
}


template<u32 S> inline BitArray8<S>
BitArray8<S>::operator~() const
{
	This r;
	for(int i = 0; i < kArraySize; ++i)
		r.bits[i] = ~this->bits[i];
	return r;
}


template<u32 S> inline BitArray8<S>
BitArray8<S>::operator|(const This& rhs) const
{
	This r(*this);
	r |= rhs;
	return r;
}


template<u32 S> inline BitArray8<S>
BitArray8<S>::operator&(const This& rhs) const
{
	This r(*this);
	r &= rhs;
	return r;
}


template<u32 S> inline BitArray8<S>
BitArray8<S>::operator^(const This& rhs) const
{
	This r(*this);
	r ^= rhs;
	return r;
}
