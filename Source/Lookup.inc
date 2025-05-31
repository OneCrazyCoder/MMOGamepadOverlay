//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// VectorMap
//-----------------------------------------------------------------------------

template<class K, class V, class C>
struct VectorMapComparator : public C
{
	typedef typename std::pair<K, V> EntryPair;
	bool operator() (const EntryPair& a, const EntryPair& b) const
	{ return C::operator() (a.first, b.first); }
};


template<class K, class V, class C> inline
void VectorMap<K, V, C>::sort()
{
	VectorMapComparator<K, V, C> comp;
	std::sort(begin(), end(), comp);
}


template<class K, class V, class C> inline
void VectorMap<K, V, C>::removeDuplicates()
{
	std::equal_to<K> comp;
	removeDuplicates(comp);
}


template<class K, class V, class C> template<class P> inline
void VectorMap<K, V, C>::removeDuplicates(P)
{
	VectorMapComparator<K, V, P> comp;
	erase(std::unique(begin(), end(), comp), end());
}


template<class K, class V, class C> inline
V& VectorMap<K, V, C>::setValue(const K& theKey, const V& theValue)
{
	iterator itr = begin() + findInsertPos(theKey);
	if( itr == end() || itr->first != theKey )
		itr = insert(itr, std::make_pair(theKey, theValue));
	else
		itr->second = theValue;

	return itr->second;
}


template<class K, class V, class C> inline
V& VectorMap<K, V, C>::addPair(const K& theKey, const V& theValue)
{
	push_back(std::make_pair(theKey, theValue));
	return back().second;
}


template<class K, class V, class C> inline
bool VectorMap<K, V, C>::erase(const K& theKey)
{
	iterator itr = find(theKey);
	if( itr != end() )
	{
		erase(itr);
		return true;
	}
	return false;
}


template<class K, class V, class C> inline
void VectorMap<K, V, C>::shrink_to_fit()
{
	if( size() < capacity() )
		VectorMap(*this).swap(*this);
}


template<class K, class V, class C> inline
typename VectorMap<K, V, C>::iterator
VectorMap<K, V, C>::find(const K& theKey)
{
	iterator itr = begin() + findInsertPos(theKey);
	if( itr != end() && itr->first != theKey )
		itr = end();
	return itr;
}


template<class K, class V, class C> inline
typename VectorMap<K, V, C>::const_iterator
VectorMap<K, V, C>::find(const K& theKey) const
{
	const_iterator itr = begin() + findInsertPos(theKey);
	if( itr != end() && itr->first != theKey )
		itr = end();
	return itr;
}


template<class K, class V, class C> inline
V& VectorMap<K, V, C>::findOrAdd(const K& theKey, const V& theDefault)
{
	iterator itr = begin() + findInsertPos(theKey);
	if( itr == end() || itr->first != theKey )
		itr = insert(itr, std::make_pair(theKey, theDefault));

	return itr->second;
}


template<class K, class V, class C> inline
int VectorMap<K, V, C>::findInsertPos(const K& theKey) const
{
	C comp;
	int left = 0;
	int right = int(size());
	while(left < right)
	{
		const int mid = (left + right) / 2;
		if( comp((*this)[mid].first, theKey) )
			left = mid + 1;
		else
			right = mid;
	}

	return left;
}


//-----------------------------------------------------------------------------
// StringToValueMap
//-----------------------------------------------------------------------------

template<class V, class I, bool S> inline
StringToValueMap<V, I, S>::StringToValueMap()
	:
	mHeadNodeIndex(0)
{
}


template<class V, class I, bool S> inline
void StringToValueMap<V, I, S>::clear()
{
	mTrie.clear();
	mValues.clear();
	mKeys.clear();
	mHeadNodeIndex = 0;
}


template<class V, class I, bool S> inline
V& StringToValueMap<V, I, S>::setValue(const Key& theKey, const V& theValue)
{
	if( mTrie.empty() )
	{
		addNewNode(theKey, theValue);
		return reinterpret_cast<V&>(mValues[0]);
	}

	// Start with the same logic as find() to see if key is already there
	// If found, update the value to the new value passed in and return it
	const FindKey aFindKey(theKey);
	int aBPNodeIdx; // node that back-pointed to key node, used in insert
	int aFoundNodeIdx = bestNodeIndexForInsert(aFindKey, &aBPNodeIdx);
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());
	if( aFindKey == mKeys[aFoundNodeIdx] )
	{
		mValues[aFoundNodeIdx] = StoredValueType(theValue);
		return reinterpret_cast<V&>(mValues[aFoundNodeIdx]);
	}

	// Key not found, need to insert it into the map w/ specified value
	int aNewNodeIdx = addNewNode(theKey, theValue);
	linkNode(aFindKey, I(aNewNodeIdx), I(aFoundNodeIdx), I(aBPNodeIdx));
	return reinterpret_cast<V&>(mValues[aNewNodeIdx]);
}


template<class V, class I, bool S> inline
void StringToValueMap<V, I, S>::reserve(int theCapacity)
{
	DBG_ASSERT(theCapacity <= (1 << (sizeof(I) * 8)));
	mTrie.reserve(theCapacity);
	mValues.reserve(theCapacity);
	mKeys.reserve(theCapacity);
}


template<class V, class I, bool S> inline
void StringToValueMap<V, I, S>::shrink_to_fit()
{
	if( mTrie.size() < mTrie.capacity() )
		std::vector<Node>(mTrie).swap(mTrie);
	if( mValues.size() < mValues.capacity() )
		ValueVector(mValues).swap(mValues);
	if( mKeys.size() < mKeys.capacity() )
		KeyVector(mKeys).swap(mKeys);
}


template<class V, class I, bool S> inline
void StringToValueMap<V, I, S>::freeKeys()
{
	KeyVector().swap(mKeys);
}


template<class V, class I, bool S> inline
V* StringToValueMap<V, I, S>::find(const Key& theKey)
{
	int aFoundIdx = findIndex(theKey);
	return reinterpret_cast<V*>(
		aFoundIdx < size() ? &mValues[aFoundIdx] : null);
}


template<class V, class I, bool S> inline
const V* StringToValueMap<V, I, S>::find(const Key& theKey) const
{
	int aFoundIdx = findIndex(theKey);
	return reinterpret_cast<const V*>(
		aFoundIdx < size() ? &mValues[aFoundIdx] : null);
}


template<class V, class I, bool S> inline
V& StringToValueMap<V, I, S>::findOrAdd(const Key& theKey, const V& theDefault)
{
	return reinterpret_cast<V&>(
		mValues[findOrAddIndex(theKey, theDefault)]);
}


template<class V, class I, bool S> inline
int StringToValueMap<V, I, S>::findIndex(const Key& theKey) const
{
	int result = size();
	if( mTrie.empty() )
		return result;

	const FindKey aSearchKey(theKey);
	int idx = bestNodeIndexForFind(aSearchKey);
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());
	if( aSearchKey == mKeys[idx] )
		result = idx;

	return result;
}


template<class V, class I, bool S> inline
int StringToValueMap<V, I, S>::findOrAddIndex(
	const Key& theKey, const V& theDefault)
{
	int aNewNodeIdx = 0;
	if( mTrie.empty() )
	{
		aNewNodeIdx = addNewNode(theKey, theDefault);
		return aNewNodeIdx;
	}

	// Start with the same logic as find() to see if key is already there
	// If found, just return the existing value
	const FindKey aFindKey(theKey);
	int aBPNodeIdx; // node that back-pointed to key node, used in insert
	int aFoundNodeIdx = bestNodeIndexForInsert(aFindKey, &aBPNodeIdx);
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());
	if( aFindKey == mKeys[aFoundNodeIdx] )
		return aFoundNodeIdx;

	// Key not found, need to insert it into the map w/ default value
	aNewNodeIdx = addNewNode(theKey, theDefault);
	linkNode(aFindKey, I(aNewNodeIdx), I(aFoundNodeIdx), I(aBPNodeIdx));
	return aNewNodeIdx;
}


template<class V, class I, bool S> inline
V& StringToValueMap<V, I, S>::quickFind(const Key& theKey)
{
	return reinterpret_cast<V&>(mValues[quickFindIndex(theKey)]);
}


template<class V, class I, bool S> inline
const V& StringToValueMap<V, I, S>::quickFind(const Key& theKey) const
{
	return reinterpret_cast<const V&>(mValues[quickFindIndex(theKey)]);
}


template<class V, class I, bool S> inline
int StringToValueMap<V, I, S>::quickFindIndex(const Key& theKey) const
{
	return bestNodeIndexForFind(theKey);
}


template<class V, class I, bool S> inline
bool StringToValueMap<V, I, S>::containsPrefix(const Key& thePrefix) const
{
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());

	bool found = false;
	findAllWithPrefix(thePrefix, &reportHasPrefix, &found);
	return found;
}


template<class V, class I, bool S> inline
void StringToValueMap<V, I, S>::findAllWithPrefix(
	const Key& thePrefix, FoundIndexCallback theCallback, void* theData) const
{
	if( mTrie.empty() || !theCallback )
		return;

	// Walk the tree searching for thePrefix until find a node whose
	// bitpos is >= the length of thePrefix (or a back-pointing node).
	const FindKey& aFindPrefix(thePrefix);
	int aCheckNodeIdx = mHeadNodeIndex;
	const int aMaxBitPos = u16(aFindPrefix.size * 8);
	int aLastCheckedNodeBitPos = -1;
	bool foundBPNode = false;
	while(mTrie[aCheckNodeIdx].bitPos < aMaxBitPos && !foundBPNode)
	{
		aLastCheckedNodeBitPos = mTrie[aCheckNodeIdx].bitPos;
		aCheckNodeIdx = bitAtPos(aFindPrefix, mTrie[aCheckNodeIdx].bitPos)
			? mTrie[aCheckNodeIdx].right
			: mTrie[aCheckNodeIdx].left;
		foundBPNode = mTrie[aCheckNodeIdx].bitPos <= aLastCheckedNodeBitPos;
	}

	// Recursively search for end nodes past this point and report results
	PrefixFindData aFindData(
		thePrefix, aFindPrefix,
		theCallback, theData);
	findPrefixRecursive(aCheckNodeIdx, aLastCheckedNodeBitPos, aFindData);
}


template<class V, class I, bool S> inline
int StringToValueMap<V, I, S>::addNewNode(const Key& theKey, const V& theValue)
{
	DBG_ASSERT(mTrie.size() < (1 << (sizeof(I) * 8)));
	mKeys.push_back(theKey);
	mValues.push_back(StoredValueType(theValue));
	Node aNewNode;
	aNewNode.bitPos = 0;
	aNewNode.left = I(mTrie.size());
	aNewNode.right = I(mTrie.size());
	mTrie.push_back(aNewNode);
	return int(mTrie.size()-1);
}


template<class V, class I, bool S> inline
void StringToValueMap<V, I, S>::linkNode(
	const FindKey& theNewKey, I theNewNode, I theFoundNode, I theBackPtr)
{
	const FindKey theFoundKey(mKeys[theFoundNode]);
	// Set new node's bitPos to first diff between its key and the found key
	for(u32 aBitPos = 0, aCharIdx = 0; /*until break*/; ++aCharIdx)
	{
		const u32 aNewKeyChar = getChar(theNewKey, aCharIdx);
		const u32 anOldKeyChar = getChar(theFoundKey, aCharIdx);
		if( const u32 aDiffBits = aNewKeyChar ^ anOldKeyChar )
		{
			mTrie[theNewNode].bitPos =
				u16(aBitPos + trailingZeroBits(aDiffBits));
			break;
		}
		aBitPos += 8;
		// Function should only be called if already know a diff exists!
		DBG_ASSERT(aBitPos <= u32(max(theNewKey.size, theFoundKey.size) * 8));
	}

	// If the first bit different between the new key and the found key is less
	// than the bitPos the BP node used to redirect to the found key, it means
	// the keys diverged further up the tree. How could this happen? Well, each
	// node checked potentially skips over several bits that are assumed to be
	// a shared prefix, and bits between those key bits are only checked at the
	// end with a final string compare. So must have skipped over the bit that
	// is different for this new unknown key during the initial tree walk! In
	// that case, must now walk the tree again and stop at the first place a
	// bitPos is >= the new node's. This means in this case theBackPtr will
	// technically be a parent node rather than a back-pointer node.
	bool aParentWentRight = false;
	if( mTrie[theNewNode].bitPos < mTrie[theBackPtr].bitPos )
	{
		theFoundNode = mHeadNodeIndex;
		#ifndef NDEBUG
		int aLastCheckedNodeBitPos = 0;
		#endif
		while(mTrie[theFoundNode].bitPos < mTrie[theNewNode].bitPos)
		{
			theBackPtr = theFoundNode;
			#ifndef NDEBUG
			aLastCheckedNodeBitPos = mTrie[theBackPtr].bitPos;
			#endif
			aParentWentRight =
				bitAtPos(theNewKey, mTrie[theBackPtr].bitPos);
			theFoundNode = aParentWentRight
				? mTrie[theBackPtr].right
				: mTrie[theBackPtr].left;
			// Search should always be aborted before actually get back-pointed
			DBG_ASSERT(
				mTrie[theFoundNode].bitPos >= mTrie[theNewNode].bitPos ||
				aLastCheckedNodeBitPos < mTrie[theFoundNode].bitPos);
		}
	}
	else
	{
		aParentWentRight = bitAtPos(theNewKey, mTrie[theBackPtr].bitPos);
	}

	// Now can link the new node into the tree properly
	if( theFoundNode == mHeadNodeIndex &&
		mTrie[theNewNode].bitPos < mTrie[mHeadNodeIndex].bitPos )
	{// Set new node as the new head node
		if( bitAtPos(theNewKey, mTrie[theNewNode].bitPos) )
			mTrie[theNewNode].left = mHeadNodeIndex;
		else
			mTrie[theNewNode].right = mHeadNodeIndex;
		mHeadNodeIndex = theNewNode;
	}
	else
	{// Insert new node between parent/back pointer node and theFoundNode
		// Have parent/BP node point to theNewNode instead of theFoundNode
		if( aParentWentRight )
			mTrie[theBackPtr].right = theNewNode;
		else
			mTrie[theBackPtr].left = theNewNode;

		// If we are inserting between a node and its own pointer to self,
		// confirm the node would go this way when searching for its own key
		// (it may have 2 pointers to self and we are using the other one),
		// before setting new node to point back to theFoundNode.
		// This may seem a pointless check since what harm would a back pointer
		// that's never reached do? But it is required for logic elsewhere that
		// every external pointer to a node is reached when searching its key.
		if( theFoundNode != theBackPtr ||
			bitAtPos(theFoundKey, mTrie[theBackPtr].bitPos) ==
				aParentWentRight )
		{
			// Have one of new node's branches point to theFoundNode
			// (both are initially set to point back to self by addNewNode)
			// Check own key at own bitPos and set the opposite branch.
			if( bitAtPos(theNewKey, mTrie[theNewNode].bitPos) )
				mTrie[theNewNode].left = theFoundNode;
			else
				mTrie[theNewNode].right = theFoundNode;
		}
	}
}


template<class V, class I, bool S> inline
int StringToValueMap<V, I, S>::bestNodeIndexForFind(
	const FindKey& theKey) const
{
	DBG_ASSERT(!mTrie.empty());

	// Find position of node in mTrie whose key most closely matches theKey.
	// This is done by checking the bit at bitPos for each node starting with
	// the head node and following the left or right child node pointer, and
	// which one followed based on if the bit is a 0 (left) or a 1 (right),
	// until encounter a request for a bit position < the prior one requested.
	// This means have found a node pointing back at itself or a prior node in
	// the trie, which signals the end of the search path.
	int aCheckNodeIdx = mHeadNodeIndex;
	int aLastCheckedNodeBitPos;
	do
	{
		aLastCheckedNodeBitPos = mTrie[aCheckNodeIdx].bitPos;
		aCheckNodeIdx = bitAtPos(theKey, mTrie[aCheckNodeIdx].bitPos)
			? mTrie[aCheckNodeIdx].right
			: mTrie[aCheckNodeIdx].left;
	} while(aLastCheckedNodeBitPos < mTrie[aCheckNodeIdx].bitPos);

	return aCheckNodeIdx;
}


template<class V, class I, bool S> inline
int StringToValueMap<V, I, S>::bestNodeIndexForInsert(
	const FindKey& theKey, int* theBPOut) const
{
	DBG_ASSERT(!mTrie.empty());
	DBG_ASSERT(theBPOut);

	// Same logic as for Find(), but also remembers which node acted as the
	// back pointer to the found node.
	int aCheckNodeIdx = mHeadNodeIndex;
	int aBackPointerNodeIdx;
	u16 aLastCheckedNodeBitPos;
	do
	{
		aBackPointerNodeIdx = aCheckNodeIdx;
		aLastCheckedNodeBitPos = mTrie[aCheckNodeIdx].bitPos;
		aCheckNodeIdx = bitAtPos(theKey, mTrie[aCheckNodeIdx].bitPos)
			? mTrie[aCheckNodeIdx].right
			: mTrie[aCheckNodeIdx].left;
	} while(aLastCheckedNodeBitPos < mTrie[aCheckNodeIdx].bitPos);

	*theBPOut = aBackPointerNodeIdx;

	return aCheckNodeIdx;
}


template<class V, class I, bool S> inline
u32 StringToValueMap<V, I, S>::getChar(
	const FindKey& theKey, u32 theIndex) const
{
	return theIndex >= u32(theKey.size) ? 0 : theKey.ptr[theIndex];
}


template<class V, class I, bool S> inline
bool StringToValueMap<V, I, S>::bitAtPos(
	const FindKey& theKey, int theBitPos) const
{
	DBG_ASSERT(theBitPos >= 0);
	const u32 aChar = getChar(theKey, u32(theBitPos) / 8U);
	const u32 aBitPosWithinChar = u32(theBitPos % 8U);
	return ((aChar >> aBitPosWithinChar) & 1U) != 0;
}


template<class V, class I, bool S> inline
void StringToValueMap<V, I, S>::findPrefixRecursive(
	int theIndex, int thePrevBitPos, PrefixFindData& thePFD) const
{
	DBG_ASSERT(thePFD.cb);

	const StringToValueMap::Node& aNode = mTrie[theIndex];
	if( aNode.bitPos <= thePrevBitPos )
	{
		// Found an end point node - but is the prefix actually valid?
		if( !thePFD.valid )
		{
			// If any key with prefix exists, due to Trie structure
			// every end point node found at this point will have it.
			// Thus this check need only be performed once to verify,
			// and is skipped entirely if freeKeys() has been used.
			if( mKeys.size() == mTrie.size() &&
				!thePFD.prefix.matches(mKeys[theIndex], true) )
			{
				thePFD.halted = true;
				return;
			}
			thePFD.valid = true;
		}

		if( !(*thePFD.cb)(*this, theIndex, thePFD.rawPrefix, thePFD.cbData) )
			thePFD.halted = true;
		return;
	}

	// Not an endpoint - recursively search child nodes
	findPrefixRecursive(aNode.left, aNode.bitPos, thePFD);
	if( !thePFD.halted && aNode.right != aNode.left )
		findPrefixRecursive(aNode.right, aNode.bitPos, thePFD);
}


template<class V, class I, bool S> inline
bool StringToValueMap<V, I, S>::reportHasPrefix(
	const This&, int, const std::string&, void* theFlagPtr)
{
	bool* aFlagPtr = (bool*)theFlagPtr;
	*aFlagPtr = true;
	return false; // halt further searching
}


template<class V, class I, bool S>
inline StringToValueMap<V, I, S>::StrictKey::StrictKey(const Key& theKey)
	: ptr(theKey.c_str()), size(int(theKey.size()))
{
}


template<class V, class I, bool S>
inline bool StringToValueMap<V, I, S>::StrictKey::matches(
	const Key& theKey, bool asPrefixOnly) const
{
	if( asPrefixOnly )
		return theKey.compare(0, this->size, this->ptr) == 0;
	return theKey == this->ptr;
}


template<class V, class I, bool S>
inline StringToValueMap<V, I, S>::LenientKey::LenientKey(const Key& theKey)
	: ptr(buf), size(0), dynamic(false)
{
	if( theKey.size() > sizeof(buf) - 1 )
	{
		this->ptr = new char[theKey.size() + 1];
		this->dynamic = true;
	}

	bool lastWasDigit = false;
	bool pendingDash = false;
	for(const char* c = theKey.c_str(); *c; ++c)
	{
		u8 ch(*c);
		if( ch <= ' ' || ch == '_' )
			continue;
		if( ch == '-' )
		{
			if( lastWasDigit )
				pendingDash = true;
			lastWasDigit = false;
			continue;
		}
		const bool isDigit = (ch >= '0' && ch <= '9');
		if( pendingDash && isDigit )
			ptr[size++] = '-';
		pendingDash = false;
		lastWasDigit = isDigit;
		if( (ch & 0x80) == 0 )
			ch = u8(::toupper(ch));
		ptr[size++] = ch;
	}
	ptr[size] = 0; // ensure null termination
}


template<class V, class I, bool S>
inline bool StringToValueMap<V, I, S>::LenientKey::matches(
	const Key& theKey, bool asPrefixOnly) const
{
	int matchLen = 0;

	const char* a = this->ptr;
	const char* b = theKey.c_str();

	bool lastWasDigit = false;
	bool pendingDash = false;
	while(*b && matchLen < size)
	{
		u8 ch(*b++);
		if( ch <= ' ' || ch == '_' )
			continue;
		if( ch == '-' )
		{
			if( lastWasDigit )
				pendingDash = true;
			lastWasDigit = false;
			continue;
		}
		const bool isDigit = (ch >= '0' && ch <= '9');
		if( pendingDash && isDigit )
		{
			if( u8(*a++) != '-' )
				return false;
			++matchLen;
		}
		pendingDash = false;
		lastWasDigit = isDigit;
		if( !(ch & 0x80) )
			ch = u8(::toupper(ch));
		if( u8(*a++) != ch )
			return false;
		++matchLen;
	}

	if( matchLen < size )
		return false;

	// When matching prefix, don't care about trailing theKey chars
	if( asPrefixOnly )
		return true;

	// Make sure theKey has no valid trail chars left
	while(*b)
	{
		u8 ch(*b++);
		if( ch <= ' ' || ch == '_' || ch == '-' )
			continue;
		return false;
	}

	return true;
}
