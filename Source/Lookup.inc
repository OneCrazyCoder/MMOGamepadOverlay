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

template<class V, class I> inline
StringToValueMap<V, I>::StringToValueMap()
	:
	mHeadNodeIndex(0)
{
}


template<class V, class I> inline
void StringToValueMap<V, I>::clear()
{
	mTrie.clear();
	mValues.clear();
	mKeys.clear();
	mHeadNodeIndex = 0;
}


template<class V, class I> inline
V& StringToValueMap<V, I>::setValue(const Key& theKey, const V& theValue)
{
	if( mTrie.empty() )
	{
		addNewNode(theKey, theValue);
		return reinterpret_cast<V&>(mValues[0]);
	}

	// Start with the same logic as find() to see if key is already there
	// If found, update the value to the new value passed in and return it
	I aBPNodeIdx; // node that back-pointed to key node, used in insert
	I aFoundNodeIdx = bestNodeIndexForInsert(theKey, &aBPNodeIdx);
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());
	if( theKey == mKeys[aFoundNodeIdx] )
	{
		mValues[aFoundNodeIdx] = StoredValueType(theValue);
		return reinterpret_cast<V&>(mValues[aFoundNodeIdx]);
	}

	// Key not found, need to insert it into the map w/ specified value
	return reinterpret_cast<V&>(mValues[
		insertNewPair(theKey, theValue, aFoundNodeIdx, aBPNodeIdx)]);
}


template<class V, class I> inline
void StringToValueMap<V, I>::reserve(size_t theCapacity)
{
	mTrie.reserve(theCapacity);
	mValues.reserve(theCapacity);
	mKeys.reserve(theCapacity);
}


template<class V, class I> inline
void StringToValueMap<V, I>::shrink_to_fit()
{
	if( mTrie.size() < mTrie.capacity() )
		std::vector<Node>(mTrie).swap(mTrie);
	if( mValues.size() < mValues.capacity() )
		ValueVector(mValues).swap(mValues);
	if( mKeys.size() < mKeys.capacity() )
		KeyVector(mKeys).swap(mKeys);
}


template<class V, class I> inline
void StringToValueMap<V, I>::freeKeys()
{
	KeyVector().swap(mKeys);
}


template<class V, class I> inline
V* StringToValueMap<V, I>::find(const Key& theKey)
{
	if( mTrie.empty() )
		return null;

	const I idx = bestNodeIndexForFind(theKey);
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());
	if( theKey == mKeys[idx] )
		return reinterpret_cast<V*>(&mValues[idx]);

	return null;
}


template<class V, class I> inline
const V* StringToValueMap<V, I>::find(const Key& theKey) const
{
	if( mTrie.empty() )
		return null;

	const I idx = bestNodeIndexForFind(theKey);
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());
	if( theKey == mKeys[idx] )
		return reinterpret_cast<const V*>(&mValues[idx]);

	return null;
}


template<class V, class I> inline
bool StringToValueMap<V, I>::containsPrefix(const Key& thePrefix) const
{
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());
	if( mTrie.empty() )
		return false;

	// Walk the tree searching for thePrefix until find a node whose
	// bitpos is >= the length of thePrefix (or a back-pointing node).
	I aCheckNodeIdx = mHeadNodeIndex;
	u16 aMaxBitPos = u16(thePrefix.length() * 8);
	s32 aLastCheckedNodeBitPos = -1;
	bool foundBPNode = false;
	while(mTrie[aCheckNodeIdx].bitPos < aMaxBitPos && !foundBPNode)
	{
		aLastCheckedNodeBitPos = mTrie[aCheckNodeIdx].bitPos;
		aCheckNodeIdx = getBitAtPos(thePrefix, mTrie[aCheckNodeIdx].bitPos)
			? mTrie[aCheckNodeIdx].right
			: mTrie[aCheckNodeIdx].left;
		foundBPNode = mTrie[aCheckNodeIdx].bitPos <= aLastCheckedNodeBitPos;
	}

	// Found node should be a key starting with thePrefix, if any has it
	return !mKeys[aCheckNodeIdx].compare(0, thePrefix.length(), thePrefix);
}


template<class V, class I> inline
V& StringToValueMap<V, I>::findOrAdd(const Key& theKey, const V& theDefault)
{
	if( mTrie.empty() )
	{
		addNewNode(theKey, theDefault);
		return reinterpret_cast<V&>(mValues[0]);
	}

	// Start with the same logic as find() to see if key is already there
	// If found, just return the existing value
	I aBPNodeIdx; // node that back-pointed to key node, used in insert
	I aFoundNodeIdx = bestNodeIndexForInsert(theKey, &aBPNodeIdx);
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());
	if( theKey == mKeys[aFoundNodeIdx] )
		return reinterpret_cast<V&>(mValues[aFoundNodeIdx]);

	// Key not found, need to insert it into the map w/ default value
	return reinterpret_cast<V&>(mValues[
		insertNewPair(theKey, theDefault, aFoundNodeIdx, aBPNodeIdx)]);
}


template<class V, class I> inline
I StringToValueMap<V, I>::findIndex(const Key& theKey) const
{
	if( mTrie.empty() )
		return 0;

	I idx = bestNodeIndexForFind(theKey);
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());
	if( theKey == mKeys[idx] )
		return idx;

	return I(mValues.size());
}


template<class V, class I> inline
I StringToValueMap<V, I>::findOrAddIndex(
	const Key& theKey, const V& theDefault)
{
	if( mTrie.empty() )
	{
		addNewNode(theKey, theDefault);
		return 0;
	}

	I aBPNodeIdx;
	I aFoundNodeIdx = bestNodeIndexForInsert(theKey, &aBPNodeIdx);
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());
	if( theKey == mKeys[aFoundNodeIdx] )
		return aFoundNodeIdx;

	// Key not found, need to insert it into the map w/ default value
	return insertNewPair(theKey, theDefault, aFoundNodeIdx, aBPNodeIdx);
}


template<class V, class I> inline
V& StringToValueMap<V, I>::quickFind(const Key& theKey)
{
	return reinterpret_cast<V&>(mValues[bestNodeIndexForFind(theKey)]);
}


template<class V, class I> inline
const V& StringToValueMap<V, I>::quickFind(const Key& theKey) const
{
	return reinterpret_cast<const V&>(mValues[bestNodeIndexForFind(theKey)]);
}


template<class V, class I> inline
I StringToValueMap<V, I>::quickFindIndex(const Key& theKey) const
{
	return bestNodeIndexForFind(theKey);
}


template<class V, class I> inline
void StringToValueMap<V, I>::findAllWithPrefix(
	const Key& thePrefix, IndexVector& out) const
{
	if( mTrie.empty() )
		return;

	// Walk the tree searching for thePrefix until find a node whose
	// bitpos is >= the length of thePrefix (or a back-pointing node).
	I aCheckNodeIdx = mHeadNodeIndex;
	u16 aMaxBitPos = u16(thePrefix.length() * 8);
	s32 aLastCheckedNodeBitPos = -1;
	bool foundBPNode = false;
	while(mTrie[aCheckNodeIdx].bitPos < aMaxBitPos && !foundBPNode)
	{
		aLastCheckedNodeBitPos = mTrie[aCheckNodeIdx].bitPos;
		aCheckNodeIdx = getBitAtPos(thePrefix, mTrie[aCheckNodeIdx].bitPos)
			? mTrie[aCheckNodeIdx].right
			: mTrie[aCheckNodeIdx].left;
		foundBPNode = mTrie[aCheckNodeIdx].bitPos <= aLastCheckedNodeBitPos;
	}

	// Add end points of every branch further down the tree to 'result'
	struct CheckNode { s32 prevBitPos; I index; };
	std::vector<CheckNode> aCheckQueue;
	CheckNode aCheckNode;
	aCheckNode.prevBitPos = aLastCheckedNodeBitPos;
	aCheckNode.index = aCheckNodeIdx;
	aCheckQueue.push_back(aCheckNode);

	bool addedIndex = false;
	while(!aCheckQueue.empty())
	{
		aCheckNodeIdx = aCheckQueue.back().index;
		const s32 aPrevBitPos = aCheckQueue.back().prevBitPos;
		aCheckQueue.pop_back();

		if( mTrie[aCheckNodeIdx].bitPos <= aPrevBitPos )
		{// Reached an end point node, add it to results list
			// Make sure first found result actually has the prefix
			// (only checked if has not used freeKeys())
			// If first result is correct, rest do not need to be checked
			if( !addedIndex && mKeys.size() == mTrie.size() &&
				mKeys[aCheckNodeIdx].compare(
					0, thePrefix.length(), thePrefix) )
			{
				return;
			}

			addedIndex = true;
			out.push_back(aCheckNodeIdx);
		}
		else
		{// Add node's children to the check queue
			aCheckNode.prevBitPos = mTrie[aCheckNodeIdx].bitPos;
			aCheckNode.index = mTrie[aCheckNodeIdx].left;
			aCheckQueue.push_back(aCheckNode);
			if( mTrie[aCheckNodeIdx].left != mTrie[aCheckNodeIdx].right )
			{// Only add right child if different from left
				aCheckNode.index = mTrie[aCheckNodeIdx].right;
				aCheckQueue.push_back(aCheckNode);
			}
		}
	}
}


template<class V, class I> inline
I StringToValueMap<V, I>::addNewNode(const Key& theKey, const V& theValue)
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


template<class V, class I> inline
I StringToValueMap<V, I>::insertNewPair(
	const Key& theKey, const V& theValue, I theFoundIndex, I theBPIndex)
{
	// Create new node
	I aNewNodeIdx = addNewNode(theKey, theValue);

	// Set new node's bitPos to first diff between its key and the found key
	mTrie[aNewNodeIdx].bitPos = getFirstBitDiff(theKey, mKeys[theFoundIndex]);

	// If the first bit different between the new key and the found key is less
	// than the bitPos the BP node used to redirect to the found key, it means
	// the keys diverged further up the tree. How could this happen? Well, each
	// node checked potentially skips over several bits that are assumed to be
	// a shared prefix, and bits between those key bits are only checked at the
	// end with a final string compare. So must have skipped over the bit that
	// is different for this new unknown key during the initial tree walk! In
	// that case, must now walk the tree again and stop at the first place a
	// bitPos is >= the new node's. This means in this case theBPIndex will
	// technically be a parent node rather than a back-pointer node.
	u8 aParentWentRight;
	if( mTrie[aNewNodeIdx].bitPos < mTrie[theBPIndex].bitPos )
	{
		theFoundIndex = mHeadNodeIndex;
		#ifndef NDEBUG
		u16 aLastCheckedNodeBitPos = 0;
		#endif
		while(mTrie[theFoundIndex].bitPos < mTrie[aNewNodeIdx].bitPos)
		{
			theBPIndex = theFoundIndex;
			#ifndef NDEBUG
			aLastCheckedNodeBitPos = mTrie[theBPIndex].bitPos;
			#endif
			aParentWentRight = getBitAtPos(theKey, mTrie[theBPIndex].bitPos);
			theFoundIndex = aParentWentRight
				? mTrie[theBPIndex].right
				: mTrie[theBPIndex].left;
			// Search should always be aborted before actually get back-pointed
			DBG_ASSERT(
				mTrie[theFoundIndex].bitPos >= mTrie[aNewNodeIdx].bitPos ||
				aLastCheckedNodeBitPos < mTrie[theFoundIndex].bitPos);
		}
	}
	else
	{
		aParentWentRight = getBitAtPos(theKey, mTrie[theBPIndex].bitPos);
	}

	// Now can link the new node into the tree properly
	if( theFoundIndex == mHeadNodeIndex &&
		mTrie[aNewNodeIdx].bitPos < mTrie[mHeadNodeIndex].bitPos )
	{// Set new node as the new head node
		if( getBitAtPos(theKey, mTrie[aNewNodeIdx].bitPos) )
			mTrie[aNewNodeIdx].left = mHeadNodeIndex;
		else
			mTrie[aNewNodeIdx].right = mHeadNodeIndex;
		mHeadNodeIndex = aNewNodeIdx;
	}
	else
	{// Insert new node between parent/back pointer node and theFoundIndex node
		// Have parent/BP node point to aNewNodeIdx instead of theFoundIndex
		if( aParentWentRight )
			mTrie[theBPIndex].right = aNewNodeIdx;
		else
			mTrie[theBPIndex].left = aNewNodeIdx;

		// If we are inserting between a node and its own pointer to self,
		// confirm the node would go this way when searching for its own key
		// (it may have 2 pointers to self and we are using the other one),
		// before setting new node to point back to theFoundIndex.
		// This may seem a pointless check since what harm would a back pointer
		// that's never reached do? But it is required for logic elsewhere that
		// every external pointer to a node is reached when searching its key.
		if( theFoundIndex != theBPIndex ||
			getBitAtPos(mKeys[theFoundIndex], mTrie[theBPIndex].bitPos) ==
				aParentWentRight )
		{
			// Have one of new node's branches point to theFoundIndex
			// (both are initially set to point back to self by addNewNode)
			// Check own key at own bitPos and set the opposite branch.
			if( getBitAtPos(theKey, mTrie[aNewNodeIdx].bitPos) )
				mTrie[aNewNodeIdx].left = theFoundIndex;
			else
				mTrie[aNewNodeIdx].right = theFoundIndex;
		}
	}

	return aNewNodeIdx;
}


template<class V, class I> inline
I StringToValueMap<V, I>::bestNodeIndexForFind(const Key& theKey) const
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


template<class V, class I> inline
I StringToValueMap<V, I>::bestNodeIndexForInsert(
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


template<class V, class I> inline
u8 StringToValueMap<V, I>::getChar(const Key& theKey, u16 theIndex) const
{
	return theIndex >= theKey.size() ? 0 : (u8)theKey[theIndex];
}


template<class V, class I> inline
u8 StringToValueMap<V, I>::getBitAtPos(const Key& theKey, u16 theBitPos) const
{
	return (getChar(theKey, theBitPos / 8) >> (theBitPos % 8)) & 1U;
}


template<class V, class I> inline
u16 StringToValueMap<V, I>::getFirstBitDiff(
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
