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
size_t VectorMap<K, V, C>::findInsertPos(const K& theKey) const
{
	C comp;
	size_t left = 0;
	size_t right = size();
	while(left < right)
	{
		const size_t mid = (left + right) / 2;
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

template<class V, class I, KeyConvFunc F> inline
StringToValueMap<V, I, F>::StringToValueMap()
	:
	mHeadNodeIndex(0)
{
}


template<class V, class I, KeyConvFunc F> inline
void StringToValueMap<V, I, F>::clear()
{
	mTrie.clear();
	mValues.clear();
	mKeys.clear();
	mHeadNodeIndex = 0;
}


template<class V, class I, KeyConvFunc F> inline
V& StringToValueMap<V, I, F>::setValue(const Key& theKey, const V& theValue)
{
	if( mTrie.empty() )
	{
		addNewNode(theKey, theValue);
		return reinterpret_cast<V&>(mValues[0]);
	}

	// Start with the same logic as find() to see if key is already there
	// If found, update the value to the new value passed in and return it
	const Key& aConvKey = F ? F(theKey) : theKey;
	I aBPNodeIdx; // node that back-pointed to key node, used in insert
	I aFoundNodeIdx = bestNodeIndexForInsert(aConvKey, &aBPNodeIdx);
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());
	if( aConvKey == (F ? F(mKeys[aFoundNodeIdx]) : mKeys[aFoundNodeIdx]) )
	{
		mValues[aFoundNodeIdx] = StoredValueType(theValue);
		return reinterpret_cast<V&>(mValues[aFoundNodeIdx]);
	}

	// Key not found, need to insert it into the map w/ specified value
	I aNewNodeIdx = addNewNode(theKey, theValue);
	insertNode(aConvKey, aNewNodeIdx, aFoundNodeIdx, aBPNodeIdx);
	return reinterpret_cast<V&>(mValues[aNewNodeIdx]);
}


template<class V, class I, KeyConvFunc F> inline
void StringToValueMap<V, I, F>::reserve(size_t theCapacity)
{
	mTrie.reserve(theCapacity);
	mValues.reserve(theCapacity);
	mKeys.reserve(theCapacity);
}


template<class V, class I, KeyConvFunc F> inline
void StringToValueMap<V, I, F>::shrink_to_fit()
{
	if( mTrie.size() < mTrie.capacity() )
		std::vector<Node>(mTrie).swap(mTrie);
	if( mValues.size() < mValues.capacity() )
		ValueVector(mValues).swap(mValues);
	if( mKeys.size() < mKeys.capacity() )
		KeyVector(mKeys).swap(mKeys);
}


template<class V, class I, KeyConvFunc F> inline
void StringToValueMap<V, I, F>::freeKeys()
{
	KeyVector().swap(mKeys);
}


template<class V, class I, KeyConvFunc F> inline
V* StringToValueMap<V, I, F>::find(const Key& theKey)
{
	I aFoundIdx = findIndex(theKey);
	return reinterpret_cast<V*>(
		aFoundIdx < size() ? &mValues[aFoundIdx] : null);
}


template<class V, class I, KeyConvFunc F> inline
const V* StringToValueMap<V, I, F>::find(const Key& theKey) const
{
	I aFoundIdx = findIndex(theKey);
	return reinterpret_cast<const V*>(
		aFoundIdx < size() ? &mValues[aFoundIdx] : null);
}


template<class V, class I, KeyConvFunc F> inline
V& StringToValueMap<V, I, F>::findOrAdd(const Key& theKey, const V& theDefault)
{
	return reinterpret_cast<V&>(
		mValues[findOrAddIndex(theKey, theDefault)]);
}


template<class V, class I, KeyConvFunc F> inline
I StringToValueMap<V, I, F>::findIndex(const Key& theKey) const
{
	I result = size();
	if( mTrie.empty() )
		return result;

	const Key& aConvKey = F ? F(theKey) : theKey;
	I idx = bestNodeIndexForFind(aConvKey);
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());
	if( aConvKey == (F ? F(mKeys[idx]) : mKeys[idx]) )
		result = idx;

	return result;
}


template<class V, class I, KeyConvFunc F> inline
I StringToValueMap<V, I, F>::findOrAddIndex(
	const Key& theKey, const V& theDefault)
{
	I aNewNodeIdx = 0;
	if( mTrie.empty() )
	{
		aNewNodeIdx = addNewNode(theKey, theDefault);
		return aNewNodeIdx;
	}

	// Start with the same logic as find() to see if key is already there
	// If found, just return the existing value
	const Key& aConvKey = F ? F(theKey) : theKey;
	I aBPNodeIdx; // node that back-pointed to key node, used in insert
	I aFoundNodeIdx = bestNodeIndexForInsert(aConvKey, &aBPNodeIdx);
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());
	if( aConvKey == (F ? F(mKeys[aFoundNodeIdx]) : mKeys[aFoundNodeIdx]) )
		return aFoundNodeIdx;

	// Key not found, need to insert it into the map w/ default value
	aNewNodeIdx = addNewNode(theKey, theDefault);
	insertNode(aConvKey, aNewNodeIdx, aFoundNodeIdx, aBPNodeIdx);
	return aNewNodeIdx;
}


template<class V, class I, KeyConvFunc F> inline
V& StringToValueMap<V, I, F>::quickFind(const Key& theKey)
{
	return reinterpret_cast<V&>(mValues[quickFindIndex(theKey)]);
}


template<class V, class I, KeyConvFunc F> inline
const V& StringToValueMap<V, I, F>::quickFind(const Key& theKey) const
{
	return reinterpret_cast<const V&>(mValues[quickFindIndex(theKey)]);
}


template<class V, class I, KeyConvFunc F> inline
I StringToValueMap<V, I, F>::quickFindIndex(const Key& theKey) const
{
	return bestNodeIndexForFind(F ? F(theKey) : theKey);
}


template<class V, class I, KeyConvFunc F> inline
bool StringToValueMap<V, I, F>::containsPrefix(const Key& thePrefix) const
{
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());

	bool found = false;
	findAllWithPrefix(thePrefix, &reportContainsPrefix, &found);
	return found;
}


template<class V, class I, KeyConvFunc F> inline
void StringToValueMap<V, I, F>::findAllWithPrefix(
	const Key& thePrefix, FoundIndexCallback theCallback, void* theData) const
{
	if( mTrie.empty() || !theCallback )
		return;

	// Walk the tree searching for thePrefix until find a node whose
	// bitpos is >= the length of thePrefix (or a back-pointing node).
	const Key& aConvPrefix = F ? F(thePrefix) : thePrefix;
	I aCheckNodeIdx = mHeadNodeIndex;
	const u16 aMaxBitPos = u16(aConvPrefix.length() * 8);
	int aLastCheckedNodeBitPos = -1;
	bool foundBPNode = false;
	while(mTrie[aCheckNodeIdx].bitPos < aMaxBitPos && !foundBPNode)
	{
		aLastCheckedNodeBitPos = mTrie[aCheckNodeIdx].bitPos;
		aCheckNodeIdx = getBitAtPos(aConvPrefix, mTrie[aCheckNodeIdx].bitPos)
			? mTrie[aCheckNodeIdx].right
			: mTrie[aCheckNodeIdx].left;
		foundBPNode = mTrie[aCheckNodeIdx].bitPos <= aLastCheckedNodeBitPos;
	}

	// Recursively search for end nodes past this point and report results
	PrefixFindData aFindData =
		{ &thePrefix, &aConvPrefix, theCallback, theData, false, false };
	findPrefixRecursive(aCheckNodeIdx, aLastCheckedNodeBitPos, aFindData);
}


template<class V, class I, KeyConvFunc F> inline
I StringToValueMap<V, I, F>::addNewNode(const Key& theKey, const V& theValue)
{
	DBG_ASSERT(mTrie.size() < (1 << (sizeof(I) * 8)));
	mKeys.push_back(theKey);
	mValues.push_back(StoredValueType(theValue));
	Node aNewNode;
	aNewNode.bitPos = 0;
	aNewNode.left = I(mTrie.size());
	aNewNode.right = I(mTrie.size());
	mTrie.push_back(aNewNode);
	return I(mTrie.size()-1);
}


template<class V, class I, KeyConvFunc F> inline
void StringToValueMap<V, I, F>::insertNode(
	const Key& theConvKey, I theNewNode, I theFoundNode, I theBackPtr)
{
	// Set new node's bitPos to first diff between its key and the found key
	mTrie[theNewNode].bitPos = getFirstBitDiff(
			theConvKey,
			(F ? F(mKeys[theFoundNode]) : mKeys[theFoundNode]));

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
	u8 aParentWentRight;
	if( mTrie[theNewNode].bitPos < mTrie[theBackPtr].bitPos )
	{
		theFoundNode = mHeadNodeIndex;
		#ifndef NDEBUG
		u16 aLastCheckedNodeBitPos = 0;
		#endif
		while(mTrie[theFoundNode].bitPos < mTrie[theNewNode].bitPos)
		{
			theBackPtr = theFoundNode;
			#ifndef NDEBUG
			aLastCheckedNodeBitPos = mTrie[theBackPtr].bitPos;
			#endif
			aParentWentRight =
				getBitAtPos(theConvKey, mTrie[theBackPtr].bitPos);
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
		aParentWentRight =
			getBitAtPos(theConvKey, mTrie[theBackPtr].bitPos);
	}

	// Now can link the new node into the tree properly
	if( theFoundNode == mHeadNodeIndex &&
		mTrie[theNewNode].bitPos < mTrie[mHeadNodeIndex].bitPos )
	{// Set new node as the new head node
		if( getBitAtPos(theConvKey, mTrie[theNewNode].bitPos) )
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
			getBitAtPos(mKeys[theFoundNode], mTrie[theBackPtr].bitPos) ==
				aParentWentRight )
		{
			// Have one of new node's branches point to theFoundNode
			// (both are initially set to point back to self by addNewNode)
			// Check own key at own bitPos and set the opposite branch.
			if( getBitAtPos(theConvKey, mTrie[theNewNode].bitPos) )
				mTrie[theNewNode].left = theFoundNode;
			else
				mTrie[theNewNode].right = theFoundNode;
		}
	}
}


template<class V, class I, KeyConvFunc F> inline
I StringToValueMap<V, I, F>::bestNodeIndexForFind(const Key& theKey) const
{
	DBG_ASSERT(!mTrie.empty());

	// Find position of node in mTrie whose key most closely matches theKey.
	// This is done by checking the bit at bitPos for each node starting with
	// the head node and following the left or right child node pointer, and
	// which one followed based on if the bit is a 0 (left) or a 1 (right),
	// until encounter a request for a bit position < the prior one requested.
	// This means have found a node pointing back at itself or a prior node in
	// the trie, which signals the end of the search path.
	I aCheckNodeIdx = mHeadNodeIndex;
	u16 aLastCheckedNodeBitPos;
	do
	{
		aLastCheckedNodeBitPos = mTrie[aCheckNodeIdx].bitPos;
		aCheckNodeIdx = getBitAtPos(theKey, mTrie[aCheckNodeIdx].bitPos)
			? mTrie[aCheckNodeIdx].right
			: mTrie[aCheckNodeIdx].left;
	} while(aLastCheckedNodeBitPos < mTrie[aCheckNodeIdx].bitPos);

	return aCheckNodeIdx;
}


template<class V, class I, KeyConvFunc F> inline
I StringToValueMap<V, I, F>::bestNodeIndexForInsert(
	const Key& theKey, I* theBPOut) const
{
	DBG_ASSERT(!mTrie.empty());
	DBG_ASSERT(theBPOut);

	// Same logic as for Find(), but also remembers which node acted as the
	// back pointer to the found node.
	I aCheckNodeIdx = mHeadNodeIndex;
	I aBackPointerNodeIdx;
	u16 aLastCheckedNodeBitPos;
	do
	{
		aBackPointerNodeIdx = aCheckNodeIdx;
		aLastCheckedNodeBitPos = mTrie[aCheckNodeIdx].bitPos;
		aCheckNodeIdx = getBitAtPos(theKey, mTrie[aCheckNodeIdx].bitPos)
			? mTrie[aCheckNodeIdx].right
			: mTrie[aCheckNodeIdx].left;
	} while(aLastCheckedNodeBitPos < mTrie[aCheckNodeIdx].bitPos);

	*theBPOut = aBackPointerNodeIdx;

	return aCheckNodeIdx;
}


template<class V, class I, KeyConvFunc F> inline
u8 StringToValueMap<V, I, F>::getChar(
	const Key& theKey, u16 theIndex) const
{
	return theIndex >= theKey.size() ? 0 : (u8)theKey[theIndex];
}


template<class V, class I, KeyConvFunc F> inline
u8 StringToValueMap<V, I, F>::getBitAtPos(
	const Key& theKey, u16 theBitPos) const
{
	return (getChar(theKey, theBitPos / 8) >> (theBitPos % 8)) & 1U;
}


template<class V, class I, KeyConvFunc F> inline
u16 StringToValueMap<V, I, F>::getFirstBitDiff(
	const Key& theNewKey, const Key& theOldKey) const
{
	u16 aBitPos = 0;
	u16 aCharIdx = 0;
	while(aCharIdx != 0xFFFF)
	{
		const u8 aNewKeyChar = getChar(theNewKey, aCharIdx);
		const u8 anOldKeyChar = getChar(theOldKey, aCharIdx);
		if( const u32 aDiffBits = aNewKeyChar ^ anOldKeyChar )
			return aBitPos + u16(trailingZeroBits(aDiffBits));
		++aCharIdx;
		aBitPos += 8;
	}

	// This should never be called unless confirmed there IS a difference,
	// which means should have exited before this point
	DBG_ASSERT(false);
	return 0xFFFF;
}


template<class V, class I, KeyConvFunc F> inline
void StringToValueMap<V, I, F>::findPrefixRecursive(
	I theIndex, int thePrevBitPos, PrefixFindData& thePFD) const
{
	DBG_ASSERT(thePFD.callback);

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
			if( mKeys.size() == mTrie.size() )
			{
				const Key& aKey = F ? F(mKeys[theIndex]) : mKeys[theIndex];
				if( !!aKey.compare(
						0, thePFD.convPrefix->length(), *thePFD.convPrefix) )
				{// Prefix did not match!
					thePFD.halted = true;
					return;
				}
			}
			thePFD.valid = true;
		}

		if( !(*thePFD.callback)(theIndex, *thePFD.origPrefix, thePFD.cbData) )
			thePFD.halted = true;
		return;
	}

	// Not an endpoint - recursively search child nodes
	findPrefixRecursive(aNode.left, aNode.bitPos, thePFD);
	if( !thePFD.halted && aNode.right != aNode.left )
		findPrefixRecursive(aNode.right, aNode.bitPos, thePFD);
}


template<class V, class I, KeyConvFunc F> inline
bool StringToValueMap<V, I, F>::reportContainsPrefix(
	I, const std::string&, void* theFlagPtr)
{
	bool* aFlagPtr = (bool*)theFlagPtr;
	*aFlagPtr = true;
	return false; // halt further searching
}
