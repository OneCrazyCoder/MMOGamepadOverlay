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
void VectorMap<K, V, C>::removeDuplicates(P thePred)
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
	return insertNewPair(theKey, theValue, aFoundNodeIdx, aBPNodeIdx);
}


template<class V, class I> inline
bool StringToValueMap<V, I>::erase(const Key& theKey)
{
	// This function is quite complex, so it helps to know the terms used
	// in the comments and variable names.

	// The "key node" is the final node returned by a search for a Key, which
	// will share the same index as a key in mKeys and value in mValues.

	// The "parent" of a node is the one earlier in the tree that has one of
	// it's child pointers (left or right) point at the node, and that the node
	// pointed at has a higher bitPos than the parent's bitPos (searches start
	// at a low bitPos and get higher as search through the Key string).

	// A "back pointer" or BP node is the one that ended the search by having
	// one of its child pointers point to a node earlier in the tree (i.e. one
	// that has the same or smaller bitPos as it does). This is the signal that
	// the search has ended. Note that a node may be its own back pointer, if
	// one of its child pointers points at itself. Note also that just because
	// a node points to itself, that doesn't mean that's how the search stops
	// at that node! It might instead have passed through the node's other
	// child pointer, and any number of other nodes after that, before one of
	// them finally had a back pointer to the original node to end the search.

	// First, need to find if the key even exists!
	// Walk the tree with the same technique as bestNodeIndexForFind(),
	// but while keeping track of some other nodes along the way
	I aCheckNodeIdx = mHeadNodeIndex;
	I aBPointerToKeyNodeIdx = aCheckNodeIdx;
	I aParentOfBPointerNodeIdx;
	u16 aLastCheckedNodeBitPos;
	do
	{
		aParentOfBPointerNodeIdx = aBPointerToKeyNodeIdx;
		aBPointerToKeyNodeIdx = aCheckNodeIdx;
		aLastCheckedNodeBitPos = mTrie[aCheckNodeIdx].bitPos;
		aCheckNodeIdx = getBitAtPos(theKey, aLastCheckedNodeBitPos)
			? mTrie[aCheckNodeIdx].right
			: mTrie[aCheckNodeIdx].left;
	} while(aLastCheckedNodeBitPos < mTrie[aCheckNodeIdx].bitPos);
	const I aKeyNodeIdx = aCheckNodeIdx;

	// Early out if the key wasn't found (key does not exactly match)
	DBG_ASSERT(mKeys.size() == mValues.size());
	DBG_ASSERT(mValues.size() == mTrie.size());
	if( theKey != mKeys[aKeyNodeIdx] )
		return false;

	if( mTrie.size() == 1 )
	{// Only node left, so just delete everything
		clear();
		return true;
	}

	// For now, set the key node to be deleted, but this may change
	I aNodeToDeleteIdx = aKeyNodeIdx;	

	// How to delete the node depends on what its child pointers are set to
	const bool aKeyNodeLeftIsSelf = mTrie[aKeyNodeIdx].left == aKeyNodeIdx;
	const bool aKeyNodeRightIsSelf = mTrie[aKeyNodeIdx].right == aKeyNodeIdx;

	// Now must update related nodes to no longer point to the node to be
	// deleted, while preserving the structure needed for all other nodes
	if( aKeyNodeLeftIsSelf || aKeyNodeRightIsSelf )
	{
		// Simple case - at most only one true child node, possibly none
		// Need to know the key node's parent to update it to point elsewhere
		I aParentOfKeyNodeIdx = aParentOfBPointerNodeIdx;
		if( aBPointerToKeyNodeIdx != aKeyNodeIdx )
		{
			// Since the key node isn't its own back pointer, we don't yet know
			// for sure what its parent node is. The parent node may have been
			// several steps before encountering the back pointer, and at first
			// walk through the tree didn't yet know what we were looking for.
			// So need to walk again to find the actual parent.
			aCheckNodeIdx = mHeadNodeIndex;
			do
			{
				aParentOfKeyNodeIdx = aCheckNodeIdx;
				aLastCheckedNodeBitPos = mTrie[aCheckNodeIdx].bitPos;
				aCheckNodeIdx = getBitAtPos(theKey, aLastCheckedNodeBitPos)
					? mTrie[aCheckNodeIdx].right
					: mTrie[aCheckNodeIdx].left;
			} while(aLastCheckedNodeBitPos < mTrie[aCheckNodeIdx].bitPos &&
					aCheckNodeIdx != aKeyNodeIdx);
		}
		if( aKeyNodeLeftIsSelf && aKeyNodeRightIsSelf )
		{
			// Simplest case - key node ONLY points to self, no children
			// Just update parent of key node to point to itself instead
			if( mTrie[aParentOfKeyNodeIdx].left == aKeyNodeIdx )
				mTrie[aParentOfKeyNodeIdx].left = aParentOfKeyNodeIdx;
			else
				mTrie[aParentOfKeyNodeIdx].right = aParentOfKeyNodeIdx;
		}
		else
		{
			// Key node has one true child node (not itself)
			const I aKeyNodeTrueChildIdx =
				mTrie[aKeyNodeIdx].left == aKeyNodeIdx
					? mTrie[aKeyNodeIdx].right
					: mTrie[aKeyNodeIdx].left;
			if( aKeyNodeIdx == mHeadNodeIndex )
			{// Head node has no parent to update - set its child as new head
				mHeadNodeIndex = aKeyNodeTrueChildIdx;
			}
			else
			{
				// Have Key node's parent skip over it and point to grandchild
				if( mTrie[aParentOfKeyNodeIdx].left == aKeyNodeIdx )
					mTrie[aParentOfKeyNodeIdx].left = aKeyNodeTrueChildIdx;
				else
					mTrie[aParentOfKeyNodeIdx].right = aKeyNodeTrueChildIdx;
			}
			// Have back pointer node just point back to itself instead
			// (harmless no-op if are back pointing to self already anyway)
			if( mTrie[aBPointerToKeyNodeIdx].left == aKeyNodeIdx )
				mTrie[aBPointerToKeyNodeIdx].left = aBPointerToKeyNodeIdx;
			else
				mTrie[aBPointerToKeyNodeIdx].right = aBPointerToKeyNodeIdx;
		}
	}
	else
	{
		// Complex case - 2 unique child nodes, back pointer must not be self
		DBG_ASSERT(aBPointerToKeyNodeIdx != aKeyNodeIdx);

		// Easier solution is if the BP node has no children to deal with
		// (uses its 2 pointers to point at itself and the key node)
		if( mTrie[aBPointerToKeyNodeIdx].left == aBPointerToKeyNodeIdx ||
			mTrie[aBPointerToKeyNodeIdx].right == aBPointerToKeyNodeIdx )
		{
			// Have BP node's parent point directly to Key node,
			// have Key node take over BP node's key/value, and
			// then delete BP node instead of the Key node.
			if( mTrie[aParentOfBPointerNodeIdx].left == aBPointerToKeyNodeIdx )
				mTrie[aParentOfBPointerNodeIdx].left = aKeyNodeIdx;
			else
				mTrie[aParentOfBPointerNodeIdx].right = aKeyNodeIdx;
			mKeys[aKeyNodeIdx] = mKeys[aBPointerToKeyNodeIdx];
			mValues[aKeyNodeIdx] = mValues[aBPointerToKeyNodeIdx];
			aNodeToDeleteIdx = aBPointerToKeyNodeIdx;
		}
		else
		{
			// Most complex case - Key has 2 children and BP node has one too

			// First, find the node that acts as the back pointer to BP node
			// when treat BP node's key as the desired search key.
			I aBPToBPNodeIdx;
			Key* const aTestKey = &mKeys[aBPointerToKeyNodeIdx];
			aCheckNodeIdx = aBPointerToKeyNodeIdx;
			do
			{
				aBPToBPNodeIdx = aCheckNodeIdx;
				aLastCheckedNodeBitPos = mTrie[aCheckNodeIdx].bitPos;
				aCheckNodeIdx = getBitAtPos(*aTestKey, aLastCheckedNodeBitPos)
					? mTrie[aCheckNodeIdx].right
					: mTrie[aCheckNodeIdx].left;
			} while(aLastCheckedNodeBitPos < mTrie[aCheckNodeIdx].bitPos);
			// Should have wrapped back around to the same node again, but now
			// know the node that points back to it
			DBG_ASSERT(aCheckNodeIdx == aBPointerToKeyNodeIdx);
			
			// Have Key node take over the key/values of BP node
			mKeys[aKeyNodeIdx] = mKeys[aBPointerToKeyNodeIdx];
			mValues[aKeyNodeIdx] = mValues[aBPointerToKeyNodeIdx];

			// Have parent of BP node skip over it and point to grandchild
			const I aBPointerNodeChildIdx =
				mTrie[aBPointerToKeyNodeIdx].left == aKeyNodeIdx
					? mTrie[aBPointerToKeyNodeIdx].right
					: mTrie[aBPointerToKeyNodeIdx].left;
			if( mTrie[aParentOfBPointerNodeIdx].left == aBPointerToKeyNodeIdx )
				mTrie[aParentOfBPointerNodeIdx].left = aBPointerNodeChildIdx;
			else
				mTrie[aParentOfBPointerNodeIdx].right = aBPointerNodeChildIdx;

			// Have back-pointer-to-back-pointer node point to Key node instead
			if( mTrie[aBPToBPNodeIdx].left == aBPointerToKeyNodeIdx )
				mTrie[aBPToBPNodeIdx].left = aKeyNodeIdx;
			else
				mTrie[aBPToBPNodeIdx].right = aKeyNodeIdx;

			// Now delete back pointer node instead of Key node
			aNodeToDeleteIdx = aBPointerToKeyNodeIdx;
		}
	}

	// Now actually delete a node by using the swap-and-pop method
	DBG_ASSERT(aNodeToDeleteIdx != mHeadNodeIndex);
	if( aNodeToDeleteIdx < mTrie.size()-1 ) // can skip "swap" if are last node
	{
		// Overwrite dead node's values with values from the last node
		mKeys[aNodeToDeleteIdx] = mKeys.back();
		mValues[aNodeToDeleteIdx] = mValues.back();
		mTrie[aNodeToDeleteIdx] = mTrie.back();

		// Update head node index if last node happens to be head node
		if( mHeadNodeIndex == mTrie.size() - 1 )
			mHeadNodeIndex = aNodeToDeleteIdx;

		// Now need to make sure anything pointing at the last node will point
		// to its new position at aNodeToDeleteIdx. Do this by simulating a
		// search for the last node's key and updating any links to its old
		// position along the way. This will even update the node's pointers to
		// itself as well, since search always passes through a key node once
		// before actually stopping at it.
		I aCheckNodeIdx = mHeadNodeIndex;
		do
		{
			if( mTrie[aCheckNodeIdx].left == mTrie.size()-1 )
				mTrie[aCheckNodeIdx].left = aNodeToDeleteIdx;
			if( mTrie[aCheckNodeIdx].right == mTrie.size()-1 )
				mTrie[aCheckNodeIdx].right = aNodeToDeleteIdx;
			aLastCheckedNodeBitPos = mTrie[aCheckNodeIdx].bitPos;
			aCheckNodeIdx = getBitAtPos(
				mKeys[aNodeToDeleteIdx], mTrie[aCheckNodeIdx].bitPos)
					? mTrie[aCheckNodeIdx].right
					: mTrie[aCheckNodeIdx].left;
		} while(aLastCheckedNodeBitPos < mTrie[aCheckNodeIdx].bitPos);
	}

	// And finally, can pop the old copy of the last node off the vectors
	mKeys.pop_back();
	mValues.pop_back();
	mTrie.pop_back();

	return true;
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
size_t StringToValueMap<V, I>::quickFindIndex(const Key& theKey) const
{
	return bestNodeIndexForFind(theKey);
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
V& StringToValueMap<V, I>::insertNewPair(
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
		// that's never reached do? But it is equired for erase() function that
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

	return reinterpret_cast<V&>(mValues[aNewNodeIdx]);
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
