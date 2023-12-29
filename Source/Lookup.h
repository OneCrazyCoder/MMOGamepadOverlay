//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	These structures are used for storing and looking up data by a key value,
	but using std::vector as a base instead of stl map for reduced memory use,
	improved speed due to data locality, cache efficiency, ond less frequent
	memory allocations.
*/

#include "BitHacks.h" // trailingZeroBits
#include <functional> // std::less. std::equal_to

/*-----------------------------------------------------------------------------
	VectorMap
	---------
	Vector of key-value pairs sorted by key and searched with binary search.
	Generally less overhead and faster lookups than an stl map. Insert & erase
	is slower though (especially for large or slow-to-copy objects).

	If add the key/value pairs directly (using push_back(), addPair(), etc),
	make sure to call sort() once have filled the vector with entries in order
	for the rest of the functions to work properly. May also need to call
	removeDuplicates() if there's a chance of duplicate keys.

	Note that unlike an stl map, existing values can be moved in memory when
	keys are added or removed, so pointers, iterators, and references to them
	are "unstable" (can be invalidated or become dangling pointers).

	Some tips:
	*	It can be orders of magnitude faster to use addPair() repeatedly with
		a single sort() at the end than using setValue() or findorAdd()
		repeatedly when adding many keys at once to the map.

	*	If really need pointer-stable values, just store pointers or indexes to
		objects stored in a different, stable container.
//---------------------------------------------------------------------------*/

template<class K, class V, class C = std::less<K> >
class VectorMap : public std::vector<std::pair<K, V> >
{
public:
	// TYPES & CONSTANTS
	typedef typename std::pair<K, V> EntryPair;
	typedef typename std::vector<EntryPair> VectorOfPairs;
	typedef typename VectorOfPairs::iterator iterator;
	typedef typename VectorOfPairs::const_iterator const_iterator;

	// MUTATORS
	// Sets the value for specified key, or adds new key/value pair.
	// For a new key, it is added in pre-sorted position so no need to sort(),
	// though this method is much slower if adding many new keys at once.
	V& setValue(const K& theKey, const V& theValue);
	// Adds the key and value even if key already exists, and at the end, so
	// will need to call sort() later. Useful for faster method of adding a
	// bunch of key/value pairs at once and then sorting once afterward.
	V& addPair(const K& theKey, const V& theValue);
	// Sorts all entries by K using C for comparison. Required for most other
	// functions here to work properly after adding entries with addPair.
	void sort();
	// Removes any duplicate K entries (must already be sorted first!)
	void removeDuplicates();
	template<class P> void removeDuplicates(P thePred);
	// Finds the key and removes it, or returns false if it wasn't found
	bool erase(const K& theKey);
	// Can save memory for cases where build up once then only search it
	void trim() { shrink_to_fit(); }
	void shrink_to_fit();

	// ACCESSORS
	// Returns end() if theKey is not found with a binary search
	iterator find(const K& theKey);
	const_iterator find(const K& theKey) const;
	// Just returns true if theKey is found, if that's all that's needed
	bool contains(const K& theKey) const { return find(theKey) != end(); }
	// find() but if key not found, adds one with default value and returns it
	// If adds the key, it is inserted in proper position so no need to sort()
	V& findOrAdd(const K& theKey, const V& theDefault = V());
	// Returns index to insert key and maintain sorted order (or the index of
	// the key if it already exists - up to you to figure out which it is!)
	size_t findInsertPos(const K& theKey) const;

	// Functions from vector class for reference (the rest are also available)
	using VectorOfPairs::push_back;
	using VectorOfPairs::insert;
	using VectorOfPairs::erase;
	using VectorOfPairs::begin;
	using VectorOfPairs::end;
	using VectorOfPairs::back;
	using VectorOfPairs::swap;
	using VectorOfPairs::reserve;
	using VectorOfPairs::capacity;
	using VectorOfPairs::size;
};


/*-----------------------------------------------------------------------------
	StringToValueMap
	----------------

	Variant of a PATRICIA binary radix trie (Donald R. Morrison, 1968).

	An alternative to VetorMap that is generally superior when the key values
	are strings, as opposed to int's or other fast-to-compare data types.
	Very fast to add and look up keys - often faster than even a hash table
	(though this can heavily depend on implementation, compiler, and platform).
	Is particularly well-suited to sets of key strings where many have common
	sub-strings or prefixes (such as file paths).
	
	Can also take up much less memory than a hash table (or any other trie
	variant I've come across) and in a more consistent way, with each unique
	key having a fixed size (4-6 bytes) of overhead per key/value pair with no
	wasted empty slots or extra collision-handling overhead as is necessary for
	all non-perfect hash tables. Of course, the underlying vectors themselves
	can allocate more memory than is actually needed (a necessary evil to get
	the cache-efficient performance enhancement of using contiguous memory),
	but this can be controlled using reserve() and trim().

	When it is possible to do, using freeKeys() and then only quickFind() after
	adding all keys will significantly speed up search times, and greatly
	reduce memory used, making it much faster and smaller than VectorMap.

	Some things to be aware of:

	*	There is no way to erase a key/value pairs without erasing everything.
		I have a function written that does it (contact me if really needed),
		but it is incredibly complex and slow. This structure was just not
		designed for random erasing of keys. Instead of attempting to erase
		keys, just set their associated values to some sentinal value that
		indicates it is no longer valid (null, -1, etc).

	*	Searches are case-sensitive, as this depends on direct bit comparisons
		rather than any form of lexicographical comparisons.

	*	No sorts happen, so although can iterate over the key & value vectors
		(for serialization and the like), they will be in no particular order
		(unlike VectorMap or stl map which are sorted). This also means can't
		do range searches and such, only single key finds (though can use
		findAllWithPrefix() to get all keys with a given prefix).

	*	Unlike an stl map, existing values can be moved in memory when keys
		are added or removed, so pointers, iterators, and references to
		them are "unstable" (can be invalidated or become dangling pointers).

	*	Maximum .size() of a key std::string must be <= 8,190

	*	'V' must be valid for use in a vector (copyable, etc).

	*	'I' must be an unsigned integer type, and is the type used for array
		indexes, which thus determines the size of each node (2 I's + a u16)
		and maximum key/value pairs that can be stored (u8 = 256 keys/values,
		u16 = 65,536 keys/values, etc).
//---------------------------------------------------------------------------*/
template<class V, class I = u16>
class StringToValueMap
{
public:
	// TYPES & CONSTANTS
	typedef std::string Key;
	typedef V Value;
	typedef I IndexType;
	typedef std::vector<Key> KeyVector;
	// Avoid vector<bool> issues by using vector<u8> instead when 'V' is bool
	template<class T> struct ValueTrait { typedef T Type; };
	template<> struct ValueTrait<bool> { typedef u8 Type; };
	typedef typename ValueTrait<V>::Type StoredValueType;
	typedef std::vector<StoredValueType> ValueVector;
	typedef std::vector<IndexType> IndexVector;

	// CONSTRUCTOR/DESTRUCTOR
	StringToValueMap();

	// MUTATORS
	// Removes all entries (call trim() after to also free reserved memory)
	void clear();
	// Sets the value for specified key, or adds new key/value pair
	V& setValue(const Key& theKey, const V& theValue);
	// Reserves memory for number of entries (unique keys) expect to add
	void reserve(size_t theCapacity);
	// Can save memory for cases where build up once then only search it
	void trim() { shrink_to_fit(); }
	void shrink_to_fit();
	// Frees memory used to store copies of the original string keys, which
	// are not needed any more if done adding/removing keys and only use
	// quickFind() from then on. Invalidates the use of setValue(), erase(),
	// find(), contains(), and findOrAdd() (will all assert) until next time
	// clear() is used, and of course makes getKeyVector() become empty().
	void freeKeys();
	
	// ACCESSORS
	// Returns pointer to value for given key (null if not found/removed)
	// Non-const version allows changing the value directly using the pointer
	V* find(const Key& theKey);
	const V* find(const Key& theKey) const;
	// Just returns true if theKey is found, if that's all that's needed
	bool contains(const Key& theKey) const { return find(theKey) != null; }
	// Like find(), but if key not found, adds it (with default value) first.
	// Returns a direct reference to the value, since can't return 'null'.
	V& findOrAdd(const Key& theKey, const V& theDefault = V());
	// Faster version of find if already know key value is valid and contained,
	// and only search method that works after use freeKeys(), because it skips
	// final check for exact matching key. If the requested key is NOT in the
	// map, returns a valid value from some other key (or asserts if empty()).
	V& quickFind(const Key& theKey);
	const V& quickFind(const Key& theKey) const;
	// Acts like quickFind but returns index into values() (and keys() if it
	// hasn't been freed). Provided largely just for debugging purposes.
	size_t quickFindIndex(const Key& theKey) const;
	// Returns a list of indexes for all key/value pairs where the key
	// starts with the given prefix (in no particular order), which can then
	// be used to index into keys()/values(). Can be used after freeKeys()
	// but result will not be valid if no keys actually have given prefix!
	IndexVector findAllWithPrefix(const Key& thePrefix) const;
	// Const access to the vectors of keys & values, for direct iteration.
	// These are not guaranteed to be in any particular order, except in
	// relation to each other (i.e. keyVector()[idx] returns the key for
	// the associated value valueVector()[idx]).
	const KeyVector& keys() const { return mKeys; }
	const ValueVector& values() const { return mValues; }
	bool empty() const { return mValues.empty(); }
	size_t size() const { return mValues.size(); }

private:
	// PRIVATE FUNCTIONS
	I addNewNode(const Key& theKey, const V& theValue);
	V& insertNewPair(const Key&, const V&, I theFoundIndex, I theBackPointer);
	I bestNodeIndexForFind(const Key& theKey) const;
	I bestNodeIndexForInsert(const Key& theKey, I* theBPOut) const;
	u8 getChar(const Key& theKey, u16 theIndex) const;
	u8 getBitAtPos(const Key& theKey, u16 theBitPos) const;
	u16 getFirstBitDiff(const Key& theNewKey, const Key& theOldKey) const;

	// PRIVATE DATA
	struct Node { u16 bitPos; I left, right; };
	std::vector<Node> mTrie;
	ValueVector mValues;
	KeyVector mKeys;
	u16 mHeadNodeIndex;
};

#include "Lookup.inc"
