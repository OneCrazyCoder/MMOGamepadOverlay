//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#include "LayoutEditor.h"
#include "Dialogs.h"
#include "HotspotMap.h"
#include "InputMap.h"
#include "InputTranslator.h"
#include "Menus.h"
#include "Profile.h"
#include "Resources/resource.h"
#include "TargetConfigSync.h"
#include "WindowManager.h"

#include <CommCtrl.h>

namespace LayoutEditor
{

//------------------------------------------------------------------------------
// Const Data
//------------------------------------------------------------------------------

enum {
kBoundBoxAnchorDrawSize = 6,
kActiveHotspotDrawSize = 5,
kChildHotspotDrawSize = 3,
kMinOffsetScale = 25,
kMaxOffsetScale = 400,
};

const char* kAnchorPropName = "Anchor";
const char* kScalePropName = "Scale";
const char* kPositionPropName = "Position";
const char* kItemSizePropName = "ItemSize";
const char* kAlignmentPropName = "Alignment";
const WCHAR* kAnchorOnlyLabel = L"Scaling:";
const WCHAR* kOffsetOnlyLabel = L"Offset:";
const WCHAR* kValueOnlyLabel = L"Value:";

enum EAlignment
{
	eAlignment_L_T,
	eAlignment_CX_T,
	eAlignment_R_T,
	eAlignment_L_CY,
	eAlignment_CX_CY,
	eAlignment_R_CY,
	eAlignment_L_B,
	eAlignment_CX_B,
	eAlignment_R_B,

	eAlignment_Num,
	eAlignment_VarString = eAlignment_Num,
	eAlignment_UseDefault,
	eAlignment_WrapX,
	eAlignment_WrapY,
};

const char* const kAlignmentStr[][2] =
{//		Droplist			Profile
	{	"Top Left",			"L, T"		}, // eAlignment_L_T
	{	"Top Center",		"CX, T"		}, // eAlignment_CX_T
	{	"Top Right",		"R, T"		}, // eAlignment_R_T
	{ 	"Center Left",		"L, CY"		}, // eAlignment_L_CY
	{ 	"Center",			"CX, CY"	}, // eAlignment_CX_CY
	{ 	"Center Right",		"R, CY"		}, // eAlignment_R_CY
	{ 	"Bottom Left",		"L, B"		}, // eAlignment_L_B
	{ 	"Bottom Center",	"CX, B"		}, // eAlignment_CX_B
	{ 	"Bottom Right",		"R, B"		}, // eAlignment_R_B
};


//------------------------------------------------------------------------------
// Local Structures
//------------------------------------------------------------------------------

struct ZERO_INIT(LayoutEntry)
{
	enum EType
	{
		// These first type values are also the literal index of the
		// only object of each of these types in the entries vector
		eType_Root,
		eType_HotspotCategory,
		eType_MenuCategory,
		eType_UIScale,

		// These types should not be used as indexes as count can vary
		eType_Hotspot,
		eType_HotspotSetAnchor,
		eType_Menu,

		eType_Num,
		eType_CategoryNum = eType_Hotspot
	} type;
	Dialogs::TreeViewDialogItem item;

	struct Shape
	{
		struct Component
		{
			std::string base;
			int offset;
			bool useDefault;
			Component() : offset(0), useDefault(true) {}
			bool operator==(const Component& rhs) const
			{
				return
					(useDefault && rhs.useDefault) ||
					(base == rhs.base && offset == rhs.offset &&
					 !useDefault && !rhs.useDefault);
			}
			bool operator!=(const Component& rhs) const
			{ return !(*this == rhs); }
		};
		Component x, y, w, h;
		std::string scale;
		EAlignment alignment;
		int wrap;
		int rangeIdx;
		std::string extraVarString; // for alignment or wrap
		Shape() : alignment(eAlignment_UseDefault), wrap(), rangeIdx() {}
		bool operator==(const Shape& rhs) const
		{
			return
				x == rhs.x && y == rhs.y && w == rhs.w && h == rhs.h &&
				wrap == rhs.wrap && extraVarString == rhs.extraVarString &&
				scale == rhs.scale && alignment == rhs.alignment;
		}
		bool operator!=(const Shape& rhs) const
		{ return !(*this == rhs); }
	} shape;

	std::vector<size_t> children;
	RECT drawnRect;
	Hotspot drawHotspot;
	float drawOffScale;
	int propSectID;
	std::string overrideAnchorName;
	int parentID;
	union { int menuID; int hotpotSetEntryIdx; };
	int rangeCount;

	LayoutEntry() : type(eType_Num) {}
};


struct ZERO_INIT(EditorState)
{
	std::vector<LayoutEntry> entries;
	std::vector<Dialogs::TreeViewDialogItem*> dialogItems;
	LayoutEntry::Shape entered, applied;
	int activeEntry;
	POINT lastMouseDragPos;
	double unappliedDeltaX, unappliedDeltaY;
	bool needsDrawPosUpdate;
	bool draggingWithMouse;
};


//------------------------------------------------------------------------------
// Static Variables
//------------------------------------------------------------------------------

static EditorState* sState = null;


//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

// Forward declares
static void promptForEditEntry();

static inline std::string numToStringDecimal(double theNum, bool asPercent)
{
	std::string result;
	if( asPercent )
	{
		result = strFormat("%.4g%%", theNum * 100.0);
		return result;
	}

	DBG_ASSERT(theNum >= 0.0);
	result = strFormat("%.4f", theNum);
	while(
		(result.size() > 6 || result[result.size()-1] == '0') &&
		result[result.size()-2] != '.')
	{
		result.resize(result.size()-1);
	}

	return result;
}


static inline std::string numToStringDecimal(float theNum, bool asPercent)
{
	return numToStringDecimal(double(theNum), asPercent);
}


static inline std::string numToStringDecimal(int theNum, bool asPercent)
{
	if( asPercent )
		return strFormat("%d%%", theNum);
	return numToStringDecimal(theNum / 100.0, false);
}


static inline bool entryIsHotspot(const LayoutEntry& theEntry)
{
	return
		theEntry.type == LayoutEntry::eType_Hotspot ||
		theEntry.type == LayoutEntry::eType_HotspotSetAnchor;
}


static inline bool entryIsHotspotSetAnchor(const LayoutEntry& theEntry)
{
	return theEntry.type == LayoutEntry::eType_HotspotSetAnchor;
}


static inline bool entryIsRangeOffsets(const LayoutEntry& theEntry)
{
	return theEntry.rangeCount > 0;
}


static inline bool entryIsRangeStart(const LayoutEntry& theEntry)
{
	return
		theEntry.type == LayoutEntry::eType_Hotspot &&
		!theEntry.children.empty() &&
		entryIsRangeOffsets(sState->entries[theEntry.children[0]]);
}


static inline bool entryIsMenu(const LayoutEntry& theEntry)
{
	return theEntry.type == LayoutEntry::eType_Menu;
}


static const LayoutEntry* getPosSourceEntry(const LayoutEntry* theEntry)
{
	DBG_ASSERT(theEntry);
	const LayoutEntry* result = theEntry;
	while(result->shape.x.useDefault && result->parentID)
		result = &sState->entries[result->parentID];
	return result;
}


static const LayoutEntry* getSizeSourceEntry(const LayoutEntry* theEntry)
{
	DBG_ASSERT(theEntry);
	const LayoutEntry* result = theEntry;
	while(result->shape.w.useDefault && result->parentID)
		result = &sState->entries[result->parentID];
	return result;
}


static const LayoutEntry* getAlignmentSourceEntry(const LayoutEntry* theEntry)
{
	DBG_ASSERT(theEntry);
	const LayoutEntry* result = theEntry;
	while(
		result->shape.alignment == eAlignment_UseDefault &&
		result->parentID)
	{
		result = &sState->entries[result->parentID];
	}
	return result;
}


static std::string fetchShapeScale(std::string theString)
{
	DBG_ASSERT(!theString.empty());
	std::string aCheckStr = Profile::expandVars(theString);
	char* end = null;
	const char* aCStr = aCheckStr.c_str();
	if( double aResultVal = strtod(aCStr, &end) )
	{
		// Allow for a % at end of the number
		if( *end == '%' )
		{
			aResultVal /= 100.0;
			++end;
		}
		// If haven't reached the end of the post-var-expanded string
		// when read in as a float or percent, then generate an 
		// appropriate string ourselves out of whatever we did get
		if( *end != '\0' )
			theString = toString(aResultVal * 100.0) + '%';
	}
	else
	{
		// No valid numberals at all (or 0), treat as no scale specified
		theString.clear();
	}

	return theString;
}


static size_t fetchComponentStringEnd(
	const std::string& theString, size_t thePos, bool nameOnly = false)
{
	const char* aCStr = theString.c_str();
	int aTagDepth = 0;
	for(bool done = false; !done;)
	{
		switch(aCStr[thePos])
		{
		case '$':
			if( aCStr[thePos+1] == '{' )
			{
				thePos += 2;
				++aTagDepth;
			}
			break;
		case '}':
			if( aTagDepth )
				--aTagDepth;
			++thePos;
			break;
		case 'x': case 'X':
			// Try to differentiate between '5 x 7' and 'CX', for example
			if( !nameOnly && thePos > 0 && !aTagDepth &&
				(u8(aCStr[thePos-1]) <= ' ' ||
				 (aCStr[thePos-1] >= '0' && aCStr[thePos-1] <= '9') ||
				 aCStr[thePos-1] == '%' || aCStr[thePos-1] == '.' ||
				 aCStr[thePos-1] == '}') )
			{
				done = true;
			}
			else
			{
				++thePos;
			}
			break;
		case ',': case ':': case '@':
			if( !aTagDepth )
				done = true;
			else
				++thePos;
			break;
		case '\0':
			done = true;
			break;
		default:
			++thePos;
			break;
		}
	}

	return thePos;
}


static bool fetchShapeComponent(
	LayoutEntry::Shape::Component& theComponent,
	const std::string& theString,
	size_t& thePos,
	bool allowAnchor)
{
	theComponent = LayoutEntry::Shape::Component();
	if( thePos >= theString.size() )
		return false;

	// First need to find the end of this component
	size_t anEndPos = fetchComponentStringEnd(theString, thePos);

	// Extract the section of the string representing this component
	theComponent.base = trim(theString.substr(thePos, anEndPos-thePos));

	if( !theComponent.base.empty() )
	{// Check if base string is actually valid (after var expansion)
		const std::string& aCheckStr = Profile::expandVars(theComponent.base);
		if( isEffectivelyEmptyString(aCheckStr) )
		{
			theComponent.base.clear();
			thePos = anEndPos;
			return false;
		}
		size_t aCheckPos = 0;
		if( allowAnchor )
			stringToCoord(aCheckStr, aCheckPos);
		else
			stringToDoubleSum(aCheckStr, aCheckPos);
		// If didn't make it to end of component string, it is invalid
		// Treat as an empty component for purposes of editing
		// (it may actually be valid elsewhere but just has a complicated
		// variable setup that were unable to parse here)
		if( aCheckPos < aCheckStr.size() )
		{
			theComponent.base.clear();
			thePos = anEndPos;
			return false;
		}
	}

	// Advance thePos to end of component section for next component or end
	thePos = anEndPos;

	if( theComponent.base.empty() )
		return false;

	// See if component string ends in a non-variable non-decimal offset value,
	// and if so bump that to the offset int and trim it off the base string
	theComponent.useDefault = false;
	// Step back from end until encounter anything that isn't a numeral
	// or a + or - operator.
	int anOffStartPos = intSize(theComponent.base.size()-1);
	while(anOffStartPos >= 0 &&
		  ((theComponent.base[anOffStartPos] >= '0' &&
			theComponent.base[anOffStartPos] <= '9') ||
		   theComponent.base[anOffStartPos] == '-' ||
		   theComponent.base[anOffStartPos] == '+' ||
		   u8(theComponent.base[anOffStartPos]) <= ' '))
	{ --anOffStartPos; }
	// If reached a decimal point, step forward again to get
	// past decimal number to next int
	if( anOffStartPos >= 0 && theComponent.base[anOffStartPos] == '.' )
	{
		while(++anOffStartPos < intSize(theComponent.base.size()) &&
			  (theComponent.base[anOffStartPos] >= '0' &&
			   theComponent.base[anOffStartPos] <= '9'))
		{ ++anOffStartPos; }
	}
	if( ++anOffStartPos < intSize(theComponent.base.size()) )
	{
		size_t aDummyPos = 0;
		theComponent.offset = int(stringToDoubleSum(
			theComponent.base.substr(anOffStartPos), aDummyPos));
		theComponent.base = theComponent.base.substr(0, anOffStartPos);
	}

	return true;
}


static std::string shapeCompToProfString(
	const LayoutEntry::Shape::Component& theComponent,
	bool asOffset = false)
{
	std::string result = theComponent.base;
	if( result.empty() )
	{
		if( asOffset && theComponent.offset >= 0 )
			result = "+";
		result += toString(theComponent.offset);
	}
	else if( theComponent.offset )
	{
		if( theComponent.offset >= 0 )
			result += std::string(" + ") + toString(theComponent.offset);
		else if( theComponent.offset < 0 )
			result += std::string(" - ") + toString(-theComponent.offset);
	}

	return result;
}


static std::string hotspotRangeToProfString(
	const LayoutEntry& theStartEntry,
	const LayoutEntry::Shape& theStartShape,
	const LayoutEntry::Shape& theOffsetsShape)
{
	std::string result = theStartEntry.overrideAnchorName;
	if( !result.empty() )
		result += ": ";
	result += shapeCompToProfString(
		theStartShape.x, theStartEntry.parentID != 0) + ", ";
	result += shapeCompToProfString(
		theStartShape.y, theStartEntry.parentID != 0);
	if( !theStartShape.w.useDefault )
	{
		result += ", " +
			shapeCompToProfString(theStartShape.w) + ", " +
			shapeCompToProfString(theStartShape.h);
	}
	result += " : " + shapeCompToProfString(theOffsetsShape.x, true);
	if( theOffsetsShape.alignment == eAlignment_WrapX )
	{
		if( !theOffsetsShape.extraVarString.empty() )
		{
			result += " @ ";
			result += theOffsetsShape.extraVarString;
		}
		else if( theOffsetsShape.wrap > 0 )
		{
			result += " @ ";
			result += toString(theOffsetsShape.wrap);
		}
	}
	result += ", " + shapeCompToProfString(theOffsetsShape.y, true);
	if( theOffsetsShape.alignment == eAlignment_WrapY )
	{
		if( !theOffsetsShape.extraVarString.empty() )
		{
			result += " @ ";
			result += theOffsetsShape.extraVarString;
		}
		else if( theOffsetsShape.wrap > 0 )
		{
			result += " @ ";
			result += toString(theOffsetsShape.wrap);
		}
	}

	return result;
}


static Hotspot shapeToHotspot(const LayoutEntry::Shape& theShape)
{
	Hotspot result;
	size_t aStrPos = 0;
	result.x = stringToCoord(Profile::expandVars(theShape.x.base), aStrPos);
	result.x.offset = s16(clamp(
		result.x.offset + theShape.x.offset,
		-0x8000, 0x7FFF));

	aStrPos = 0;
	result.y = stringToCoord(Profile::expandVars(theShape.y.base), aStrPos);
	result.y.offset = s16(clamp(
		result.y.offset + theShape.y.offset,
		-0x8000, 0x7FFF));

	aStrPos = 0;
	result.w = u16(clamp(floor(
		stringToDoubleSum(Profile::expandVars(theShape.w.base), aStrPos) +
		theShape.w.offset + 0.5),
		0, 0xFFFF));

	aStrPos = 0;
	result.h = u16(clamp(floor(
		stringToDoubleSum(Profile::expandVars(theShape.h.base), aStrPos) +
		theShape.h.offset + 0.5),
		0, 0xFFFF));

	return result;
}


static Hotspot getRangeEntryOffset(
	int theRangeIdx,
	int theStepX, int theStepY,
	int applyXEvery, int applyYEvery)
{
	Hotspot result;
	if( applyXEvery )
	{
		result.x.offset = dropTo<s16>((theRangeIdx / applyXEvery) * theStepX);
		result.y.offset = dropTo<s16>((theRangeIdx % applyXEvery) * theStepY);
	}
	else if( applyYEvery )
	{
		result.x.offset = dropTo<s16>((theRangeIdx % applyYEvery) * theStepX);
		result.y.offset = dropTo<s16>((theRangeIdx / applyYEvery) * theStepY);
	}
	else
	{
		result.x.offset = dropTo<s16>(theRangeIdx * theStepX);
		result.y.offset = dropTo<s16>(theRangeIdx * theStepY);
	}

	return result;
}


static void updateDrawHotspot(
	LayoutEntry& theEntry,
	const LayoutEntry::Shape& theShape,
	bool updateParentIfNeeded = true,
	bool updateChildren = true,
	bool updateRangeElements = true)
{
	if( !theShape.rangeIdx || updateRangeElements )
		theEntry.drawHotspot = shapeToHotspot(theShape);

	theEntry.drawOffScale =
		entryIsHotspotSetAnchor(theEntry)
			? stringToFloat(Profile::expandVars(theShape.scale))
			: 1.0f;
	if( theEntry.drawOffScale <= 0 )
		theEntry.drawOffScale = 1.0f;

	if( theEntry.parentID )
	{
		LayoutEntry& aParent = sState->entries[theEntry.parentID];
		if( updateParentIfNeeded )
			updateDrawHotspot(aParent, aParent.shape, true, false);
		theEntry.drawOffScale = aParent.drawOffScale;
		const Hotspot& aParentHotspot = aParent.drawHotspot;

		if( entryIsRangeOffsets(theEntry) && updateRangeElements )
		{
			// Manually update the range element entries
			// Parent is the range's first hotspot, this is the second, and
			// children are the rest, but range's offset values are stored here
			const int aStepX = theEntry.drawHotspot.x.offset;
			const int aStepY = theEntry.drawHotspot.y.offset;
			const int applyXEvery =
				theShape.alignment == eAlignment_WrapX ? theShape.wrap : 0;
			const int applyYEvery =
				theShape.alignment == eAlignment_WrapY ? theShape.wrap : 0;
			
			for(int i = -1, end = intSize(theEntry.children.size());
				i < end; ++i )
			{
				LayoutEntry& anEntry = i < 0
					? theEntry : sState->entries[theEntry.children[i]];
				if( i >= 0 && anEntry.shape.rangeIdx == 0 )
					continue; // range element has manual override hotspot
				anEntry.drawHotspot = getRangeEntryOffset(
					anEntry.shape.rangeIdx,
					aStepX, aStepY,
					applyXEvery, applyYEvery);
			}
		}

		if( entryIsHotspot(theEntry) &&
			theEntry.drawHotspot.x.anchor == 0 )
		{// Offset from parent position
			theEntry.drawHotspot.x.anchor = aParentHotspot.x.anchor;
			theEntry.drawHotspot.x.offset = s16(clamp(int(
				theEntry.drawHotspot.x.offset * theEntry.drawOffScale) +
				aParentHotspot.x.offset, -0x8000, 0x7FFF));
		}
		else if( theShape.x.useDefault )
		{// Inherit parent position
			DBG_ASSERT(entryIsMenu(theEntry));
			theEntry.drawHotspot.x = aParentHotspot.x;
		}

		if( entryIsHotspot(theEntry) &&
			theEntry.drawHotspot.y.anchor == 0 )
		{// Offset from parent position
			theEntry.drawHotspot.y.anchor = aParentHotspot.y.anchor;
			theEntry.drawHotspot.y.offset = s16(clamp(int(
				theEntry.drawHotspot.y.offset * theEntry.drawOffScale) +
				aParentHotspot.y.offset, -0x8000, 0x7FFF));
		}
		else if( theShape.y.useDefault )
		{// Inherit parent position
			DBG_ASSERT(entryIsMenu(theEntry));
			theEntry.drawHotspot.y = aParentHotspot.y;
		}

		if( theShape.w.useDefault ||
			theEntry.drawHotspot.w == 0 && theEntry.drawHotspot.h == 0 )
		{// Inherit parent size
			theEntry.drawHotspot.w = aParentHotspot.w;
			theEntry.drawHotspot.h = aParentHotspot.h;
		}
	}
	if( updateChildren )
	{
		for(int i = 0, end = intSize(theEntry.children.size()); i < end; ++i )
		{
			LayoutEntry& aChildEntry = sState->entries[theEntry.children[i]];
			updateDrawHotspot(aChildEntry, aChildEntry.shape, false, true,
				!entryIsRangeOffsets(theEntry));
		}
	}
}


static void applyNewLayoutProperties(bool toFile = false)
{
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry > 0);
	DBG_ASSERT(size_t(sState->activeEntry) < sState->entries.size());
	LayoutEntry& theEntry = sState->entries[sState->activeEntry];
	LayoutEntry::Shape* aNewShape = &sState->entered;
	LayoutEntry::Shape* anOldShape1 = &sState->applied;
	LayoutEntry::Shape* anOldShape2 =
		toFile ? &theEntry.shape : &sState->applied;
	const bool needNewPos =
		aNewShape->x != anOldShape1->x || aNewShape->x != anOldShape2->x ||
		aNewShape->y != anOldShape1->y || aNewShape->y != anOldShape2->y;
	const bool needNewScale =
		aNewShape->scale != anOldShape1->scale ||
		aNewShape->scale != anOldShape2->scale;
	const bool needNewSize =
		aNewShape->w != anOldShape1->w || aNewShape->w != anOldShape2->w ||
		aNewShape->h != anOldShape1->h || aNewShape->h != anOldShape2->h;
	const bool needNewAlign =
		aNewShape->alignment != anOldShape1->alignment ||
		aNewShape->alignment != anOldShape2->alignment ||
		(entryIsRangeOffsets(theEntry) &&
		 (aNewShape->wrap != anOldShape1->wrap ||
		  aNewShape->wrap != anOldShape2->wrap ||
		  aNewShape->extraVarString != anOldShape1->extraVarString ||
		  aNewShape->extraVarString != anOldShape2->extraVarString));
	if( !needNewPos && !needNewSize && !needNewScale && !needNewAlign )
		return;

	if( entryIsRangeOffsets(theEntry) )
	{
		const LayoutEntry& aRangeStartEntry =
			sState->entries[theEntry.parentID];
		DBG_ASSERT(entryIsRangeStart(aRangeStartEntry));
		const std::string& aProfileStr = hotspotRangeToProfString(
			aRangeStartEntry, aRangeStartEntry.shape, *aNewShape);
		Profile::setStr(
			aRangeStartEntry.propSectID, aRangeStartEntry.item.name,
			aProfileStr, toFile);
	}
	else if( entryIsRangeStart(theEntry) )
	{
		const LayoutEntry& aRangeOffsetsEntry =
			sState->entries[theEntry.children[0]];
		DBG_ASSERT(entryIsRangeOffsets(aRangeOffsetsEntry));
		const std::string& aProfileStr = hotspotRangeToProfString(
			theEntry, *aNewShape, aRangeOffsetsEntry.shape);
		Profile::setStr(
			theEntry.propSectID, theEntry.item.name,
			aProfileStr, toFile);
	}
	else if( entryIsHotspot(theEntry) )
	{
		std::string aProfileStr = theEntry.overrideAnchorName;
		if( !aProfileStr.empty() )
			aProfileStr += ": ";
		aProfileStr +=
			shapeCompToProfString(aNewShape->x, theEntry.parentID > 0) + ", " +
			shapeCompToProfString(aNewShape->y, theEntry.parentID > 0);
		if( !aNewShape->w.useDefault )
		{
			aProfileStr += ", " +
				shapeCompToProfString(aNewShape->w) + ", " +
				shapeCompToProfString(aNewShape->h);
		}
		if( entryIsHotspotSetAnchor(theEntry) )
		{
			Profile::setStr(
				theEntry.propSectID, kAnchorPropName,
				aProfileStr, toFile);
			if( needNewScale &&
				stringToFloat(aNewShape->scale) >= 0 )
			{
				Profile::setStr(
					theEntry.propSectID,
					kScalePropName, aNewShape->scale, toFile);
			}
		}
		else
		{
			Profile::setStr(
				theEntry.propSectID, theEntry.item.name,
				aProfileStr, toFile);
		}
	}
	else if( entryIsMenu(theEntry) )
	{
		if( needNewPos )
		{
			std::string aProfileStr;
			if( !aNewShape->x.useDefault )
			{
				aProfileStr =
					shapeCompToProfString(aNewShape->x) + ", " +
					shapeCompToProfString(aNewShape->y);
			}
			Profile::setStr(
				theEntry.propSectID, kPositionPropName, aProfileStr, toFile);
		}
		if( needNewSize )
		{
			std::string aProfileStr;
			if( !aNewShape->w.useDefault )
			{
				aProfileStr += ", " +
					shapeCompToProfString(aNewShape->w) + ", " +
					shapeCompToProfString(aNewShape->h);
			}
			Profile::setStr(
				theEntry.propSectID, kItemSizePropName, aProfileStr, toFile);
		}
		if( needNewAlign )
		{
			if( aNewShape->alignment == eAlignment_UseDefault )
			{
				Profile::setStr(theEntry.propSectID, kAlignmentPropName,
					"", toFile);
			}
			else if( aNewShape->alignment == eAlignment_VarString )
			{
				Profile::setStr(theEntry.propSectID, kAlignmentPropName,
					aNewShape->extraVarString, toFile);
			}
			else
			{
				Profile::setStr(theEntry.propSectID, kAlignmentPropName,
					kAlignmentStr[aNewShape->alignment][1], toFile);
			}
		}
	}
	else if( theEntry.type == LayoutEntry::eType_UIScale )
	{
		Profile::setVariable("UIScale", sState->entered.scale, !toFile);
	}
	else
	{
		DBG_ASSERT(false);
	}

	sState->applied = sState->entered;
	if( toFile )
	{
		theEntry.shape = sState->entered;
		if( entryIsRangeOffsets(theEntry) )
		{// Apply offset changes to individual hotspots within the range as well
			Hotspot anOffsets = shapeToHotspot(sState->entered);
			const int aStepX = anOffsets.x.offset;
			const int aStepY = anOffsets.y.offset;
			const u32 applyXEvery =
				sState->entered.alignment == eAlignment_WrapX
					? sState->entered.wrap : 0;
			const u32 applyYEvery =
				sState->entered.alignment == eAlignment_WrapY
					? sState->entered.wrap : 0;
			for(int i = 0, end = intSize(theEntry.children.size());
				i < end; ++i)
			{
				LayoutEntry& anEntry = sState->entries[theEntry.children[i]];
				if( !anEntry.shape.rangeIdx )
					continue;
				anOffsets = getRangeEntryOffset(
					anEntry.shape.rangeIdx,
					aStepX, aStepY, applyXEvery, applyYEvery);
				anEntry.shape.x.offset = anOffsets.x.offset;
				anEntry.shape.y.offset = anOffsets.y.offset;
			}
		}
		Profile::saveChangesToFile();
	}
}


static void cancelUnappliedChanges()
{
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry > 0);
	DBG_ASSERT(size_t(sState->activeEntry) < sState->entries.size());
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];
	if( !gShutdown && sState->applied != anEntry.shape )
	{
		sState->entered = anEntry.shape;
		applyNewLayoutProperties();
	}
	WindowManager::setSystemOverlayCallbacks(NULL, NULL);
	WindowManager::destroyToolbarWindow();
}


static void saveNewEntryLayout()
{
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry > 0);
	DBG_ASSERT(size_t(sState->activeEntry) < sState->entries.size());
	applyNewLayoutProperties(true);
	WindowManager::setSystemOverlayCallbacks(NULL, NULL);
	WindowManager::destroyToolbarWindow();
}


static void setInitialToolbarPos(HWND theDialog, LayoutEntry& theEntry)
{
	if( !sState || !sState->activeEntry )
		return;

	// Position the tool bar as far as possible from the object to be moved,
	// so that it is less likely to end up overlapping the object
	POINT anEntryPos;
	if( entryIsMenu(theEntry) )
	{
		const RECT& anEntryRect = WindowManager::overlayRect(
			InputMap::menuOverlayID(theEntry.menuID));
		anEntryPos.x = (anEntryRect.left + anEntryRect.right) / 2;
		anEntryPos.y = (anEntryRect.top + anEntryRect.bottom) / 2;
	}
	else
	{
		updateDrawHotspot(theEntry, theEntry.shape);
		anEntryPos = WindowManager::hotspotToOverlayPos(theEntry.drawHotspot);
	}
	RECT anOverlayRect;
	anOverlayRect = WindowManager::overlayClipRect();
	POINT anOverlayCenter;
	anOverlayCenter.x = (anOverlayRect.right - anOverlayRect.left) / 2;
	anOverlayCenter.y = (anOverlayRect.bottom - anOverlayRect.top) / 2;

	const bool useRightEdge = anEntryPos.x < anOverlayCenter.x;
	const bool useBottomEdge = anEntryPos.y < anOverlayCenter.y;

	RECT aDialogRect;
	GetWindowRect(theDialog, &aDialogRect);
	const int aDialogWidth = aDialogRect.right - aDialogRect.left;
	const int aDialogHeight = aDialogRect.bottom - aDialogRect.top;

	SetWindowPos(theDialog, NULL,
		useRightEdge
			? anOverlayRect.right - aDialogWidth
			: anOverlayRect.left,
		useBottomEdge
			? anOverlayRect.bottom - aDialogHeight
			: anOverlayRect.top,
		0, 0, SWP_NOZORDER | SWP_NOSIZE);
}


static std::pair<POINT, SIZE> getControlPos(HWND theDialog, HWND theControl)
{
	DBG_ASSERT(theDialog && theControl);
	RECT aRect;
	GetWindowRect(theControl, &aRect);
	POINT aTopLeftPt = { aRect.left, aRect.top };
	POINT aBottomRightPt = { aRect.right, aRect.bottom };
	ScreenToClient(theDialog, &aTopLeftPt);
	ScreenToClient(theDialog, &aBottomRightPt);
	SIZE aControlSize = {
		aBottomRightPt.x - aTopLeftPt.x,
		aBottomRightPt.y - aTopLeftPt.y };
	return std::make_pair(aTopLeftPt, aControlSize);
}


static std::string getControlText(HWND theDialog, int theControlID)
{
	std::string result;
	HWND hCntrl = GetDlgItem(theDialog, theControlID);
	DBG_ASSERT(hCntrl);
	if( int aStrLen = GetWindowTextLength(hCntrl) )
	{
		std::vector<WCHAR> aStrBuf(aStrLen+1);
		GetDlgItemText(theDialog, theControlID, &aStrBuf[0], aStrLen+1);
		result = narrow(&aStrBuf[0]);
	}
	return result;
}


static void adjustComboBoxDroppedWidth(HWND hCombo)
{
	DBG_ASSERT(IsWindow(hCombo));

	// Get current dropped width so we don't shrink it
	const int aCurrWidth = int(SendMessage(hCombo, CB_GETDROPPEDWIDTH, 0, 0));

	// Get number of items
	const int anItemCount = int(SendMessage(hCombo, CB_GETCOUNT, 0, 0));
	if( anItemCount <= 0 )
		return;

	HDC hdc = GetDC(hCombo);
	if( !hdc )
		return;

	// Select combo box font into DC
	HFONT hFont = (HFONT)SendMessage(hCombo, WM_GETFONT, 0, 0);
	HFONT hOldFont = NULL;
	if( hFont )
		hOldFont = (HFONT)SelectObject(hdc, hFont);

	LONG aMaxTextWidth = 0;

	for(int i = 0; i < anItemCount; ++i)
	{
		int aTextLen = int(SendMessage(hCombo, CB_GETLBTEXTLEN, i, 0));
		if( aTextLen <= 0 )
			continue;

		// +1 for null terminator
		std::vector<WCHAR> aBuffer(aTextLen + 1);
		aBuffer[0] = L'\0';

		SendMessage(hCombo, CB_GETLBTEXT, i, (LPARAM)&aBuffer[0]);

		SIZE aSize = { 0, 0 };
		if( GetTextExtentPoint32(hdc, &aBuffer[0], aTextLen, &aSize) )
			aMaxTextWidth = max(aMaxTextWidth, aSize.cx);
	}

	// Restore font and DC
	if( hOldFont )
		SelectObject(hdc, hOldFont);
	ReleaseDC(hCombo, hdc);

	// Add some padding
	aMaxTextWidth += 8;

	// Only expand, never shrink
	if( aMaxTextWidth > aCurrWidth )
		SendMessage(hCombo, CB_SETDROPPEDWIDTH, aMaxTextWidth, 0);
}


static void setupPositionControls(
	HWND theDialog,
	const LayoutEntry& theEntry,
	const LayoutEntry::Shape::Component& theComponent,
	bool isY)
{
	HWND hComboBox = GetDlgItem(theDialog,
		isY ? IDC_COMBO_Y_BASE : IDC_COMBO_X_BASE);
	HWND hEditBox = GetDlgItem(theDialog, (isY ? IDC_EDIT_Y : IDC_EDIT_X));
	SendMessage(hComboBox, CB_RESETCONTENT, 0, 0);
	if( theComponent.useDefault )
	{
		SetWindowText(hEditBox, L"");
		return;
	}

	int aDropListSize = 0;
	if( !entryIsRangeOffsets(theEntry) )
	{
		SendMessage(hComboBox,CB_ADDSTRING, 0,
			(LPARAM)(isY ? L"T" : L"L"));
		SendMessage(hComboBox, CB_ADDSTRING, 0,
			(LPARAM)(isY ? L"CY" : L"CX"));
		SendMessage(hComboBox, CB_ADDSTRING, 0,
			(LPARAM)(isY ? L"B" : L"R"));
		SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)kAnchorOnlyLabel);
		aDropListSize = 4;
	}
	const bool canBeOffsetOnly =
		entryIsHotspot(theEntry) && theEntry.parentID > 0;
	const bool mustBeOffsetOnly =
		canBeOffsetOnly && entryIsRangeOffsets(theEntry);
	const double anAnchorVal = theComponent.offset
			? 0 : stringToDouble(theComponent.base, true);
	const bool isCustomAnchor = !mustBeOffsetOnly &&
		!_isnan(anAnchorVal) && anAnchorVal != 0 && anAnchorVal < 1.0;
	// Set initial selection to one of the above if appropriate
	if( canBeOffsetOnly )
	{
		SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)kOffsetOnlyLabel);
		++aDropListSize;
	}
	if( isCustomAnchor )
	{
		// Entry is anchor-only (percent of screen area)
		SendMessage(hComboBox, CB_SETCURSEL, (WPARAM)3, 0);
		SetWindowText(hEditBox, widen(theComponent.base).c_str());
	}
	else if( canBeOffsetOnly && theComponent.base.empty() )
	{
		SendMessage(hComboBox, CB_SETCURSEL, WPARAM(aDropListSize-1), 0);
	}
	else
	{
		// Check what hotspot coord would be if read ONLY base string
		size_t aPos = 0;
		const Hotspot::Coord& aCoord = stringToCoord(
			Profile::expandVars(theComponent.base), aPos);
		// If string contained an offset or unknown characters
		// (likely ${} variables), then need to include custom string option
		bool useStringBase = aCoord.offset != 0 ||
			(!theComponent.base.empty() && aPos < theComponent.base.size());
		if( !useStringBase )
		{// Check if string is simply a known basic anchor type
			switch(aCoord.anchor)
			{
			case 0:
			case 0x0001: // L or T
				SendMessage(hComboBox, CB_SETCURSEL, 0, 0);
				break;
			case 0x8000: // CX or CY
				SendMessage(hComboBox, CB_SETCURSEL, 1, 0);
				break;
			case 0xFFFF: // R or B
				SendMessage(hComboBox, CB_SETCURSEL, 2, 0);
				break;
			default: // non-standard anchor value
				useStringBase = true;
				break;
			}
		}
		if( useStringBase )
		{
			SendMessage(hComboBox, CB_ADDSTRING, 0,
				(LPARAM)widen(theComponent.base).c_str());
			SendMessage(hComboBox, CB_SETCURSEL, WPARAM(aDropListSize), 0);
		}
	}
	adjustComboBoxDroppedWidth(hComboBox);
	if( !isCustomAnchor )
	{// Set offset field
		SetWindowText(
			hEditBox,
			((theComponent.offset >= 0 ? L"+" : L"") +
			 widen(toString(theComponent.offset))).c_str());
	}
}


static void setupSizeControls(
	HWND theDialog,
	const LayoutEntry::Shape::Component& theComponent,
	bool isH)
{
	HWND hComboBox = GetDlgItem(theDialog,
		isH ? IDC_COMBO_H_BASE : IDC_COMBO_W_BASE);
	HWND hEditBox = GetDlgItem(theDialog, (isH ? IDC_EDIT_H : IDC_EDIT_W));
	SendMessage(hComboBox, CB_RESETCONTENT, 0, 0);
	if( theComponent.useDefault )
	{
		SetWindowText(hEditBox, L"");
		return;
	}

	const bool useStringBase = !theComponent.base.empty();
	int aSelection = 0;
	SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)kValueOnlyLabel);
	if( useStringBase )
	{// Use option of custom base string + offset
		SendMessage(hComboBox, CB_ADDSTRING, 0,
			(LPARAM)widen(theComponent.base).c_str());
		aSelection = 1;
	}
	adjustComboBoxDroppedWidth(hComboBox);
	SendMessage(hComboBox, CB_SETCURSEL, (WPARAM)aSelection, 0);

	SetWindowText(hEditBox,
		((useStringBase && theComponent.offset >= 0 ? L"+" : L"") +
		 widen(toString(theComponent.offset))).c_str());
}


static bool setupScaleControls(
	HWND theDialog, const std::string& theScaleString)
{
	enum EScaleType
	{
		eScaleType_None,
		eScaleType_Number,
		eScaleType_Var,
	} aScaleType = eScaleType_None;

	float aCalculatedScale = 0;
	std::string aCalculatedScaleString;
	if( !theScaleString.empty() )
	{
		const float aSimpleScale = stringToFloat(theScaleString);
		aCalculatedScale = stringToFloat(Profile::expandVars(theScaleString));
		aCalculatedScaleString = numToStringDecimal(aCalculatedScale, false);
		if( !aCalculatedScale )
			aScaleType = eScaleType_None;
		else if( aCalculatedScale == aSimpleScale )
			aScaleType = eScaleType_Number;
		else
			aScaleType = eScaleType_Var;
	}

	switch(aScaleType)
	{
	case eScaleType_None:
		SendMessage(GetDlgItem(theDialog, IDC_EDIT_S),
			EM_SETREADONLY, true, 0);
		SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S), L"");
		ShowWindow(GetDlgItem(theDialog, IDC_SLIDER_S), true);
		ShowWindow(GetDlgItem(theDialog, IDC_EDIT_VAR), false);
		EnableWindow(GetDlgItem(theDialog, IDC_SLIDER_S), false);
		SendDlgItemMessage(theDialog, IDC_SLIDER_S,
			TBM_SETPOS, TRUE, LPARAM(100));
		break;

	case eScaleType_Number:
		SendMessage(GetDlgItem(theDialog, IDC_EDIT_S),
			EM_SETREADONLY, false, 0);
		SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S),
			widen(theScaleString).c_str());
		ShowWindow(GetDlgItem(theDialog, IDC_SLIDER_S), true);
		ShowWindow(GetDlgItem(theDialog, IDC_EDIT_VAR), false);
		EnableWindow(GetDlgItem(theDialog, IDC_SLIDER_S), true);
		SendDlgItemMessage(theDialog, IDC_SLIDER_S,
			TBM_SETPOS, TRUE, LPARAM(100 * aCalculatedScale));
		break;

	case eScaleType_Var:
		SendMessage(GetDlgItem(theDialog, IDC_EDIT_S),
			EM_SETREADONLY, true, 0);
		SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S),
			widen(aCalculatedScaleString).c_str());
		ShowWindow(GetDlgItem(theDialog, IDC_SLIDER_S), false);
		SetWindowText(GetDlgItem(theDialog, IDC_EDIT_VAR),
			widen(theScaleString).c_str());
		ShowWindow(GetDlgItem(theDialog, IDC_EDIT_VAR), true);
		break;
	}

	return aScaleType == eScaleType_Number;
}


static void setupAlignmentControls(
	HWND theDialog, const LayoutEntry::Shape& theShape)
{
	HWND hComboBox = GetDlgItem(theDialog, IDC_COMBO_ALIGN);
	SendMessage(hComboBox, CB_RESETCONTENT, 0, 0);
	if( theShape.alignment == eAlignment_UseDefault )
		return;

	for(int i = 0; i < eAlignment_Num; ++i)
	{
		SendMessage(hComboBox, CB_ADDSTRING, 0,
			(LPARAM)widen(kAlignmentStr[i][0]).c_str());
	}
	if( theShape.alignment == eAlignment_VarString )
	{
		DBG_ASSERT(!theShape.extraVarString.empty());
		SendMessage(hComboBox, CB_ADDSTRING, 0,
			(LPARAM)widen(theShape.extraVarString).c_str());
	}
	adjustComboBoxDroppedWidth(hComboBox);
	SendMessage(hComboBox, CB_SETCURSEL,
		(WPARAM)theShape.alignment, 0);
}


static bool setupRangeWrapControls(
	HWND theDialog, const LayoutEntry::Shape& theShape)
{
	enum EWrapType
	{
		eWrapType_None,
		eWrapType_Number,
		eWrapType_Var,
	} aWrapType = eWrapType_None;

	int aCalculatedWrap = 0;
	std::string aCalculatedWrapString;
	if( !theShape.extraVarString.empty() )
	{
		const int aSimpleWrap = stringToInt(theShape.extraVarString);
		aCalculatedWrap = stringToInt(
			Profile::expandVars(theShape.extraVarString));
		aCalculatedWrapString = toString(aCalculatedWrap);
		if( aCalculatedWrap <= 0 )
			aWrapType = eWrapType_None;
		else if( aCalculatedWrap == aSimpleWrap )
			aWrapType = eWrapType_Number;
		else
			aWrapType = eWrapType_Var;
	}
	else if( theShape.wrap != 0 )
	{
		aWrapType = eWrapType_Number;
	}

	switch(aWrapType)
	{
	case eWrapType_None:
		SendMessage(GetDlgItem(theDialog, IDC_EDIT_WRAP),
			EM_SETREADONLY, true, 0);
		SetWindowText(GetDlgItem(theDialog, IDC_EDIT_WRAP), L"");
		EnableWindow(GetDlgItem(theDialog, IDC_SPIN_WRAP), false);
		EnableWindow(GetDlgItem(theDialog, IDC_RADIO_X), false);
		EnableWindow(GetDlgItem(theDialog, IDC_RADIO_Y), false);
		CheckDlgButton(theDialog, IDC_RADIO_X, false);
		CheckDlgButton(theDialog, IDC_RADIO_Y, false);
		break;

	case eWrapType_Number:
		SendMessage(GetDlgItem(theDialog, IDC_EDIT_WRAP),
			EM_SETREADONLY, false, 0);
		SetWindowText(GetDlgItem(theDialog, IDC_EDIT_WRAP),
			widen(toString(theShape.wrap)).c_str());
		EnableWindow(GetDlgItem(theDialog, IDC_SPIN_WRAP), true);
		EnableWindow(GetDlgItem(theDialog, IDC_RADIO_X), true);
		EnableWindow(GetDlgItem(theDialog, IDC_RADIO_Y), true);
		CheckDlgButton(theDialog, IDC_RADIO_X,
			theShape.alignment == eAlignment_WrapY);
		CheckDlgButton(theDialog, IDC_RADIO_Y,
			theShape.alignment == eAlignment_WrapX);
		break;

	case eWrapType_Var:
		SendMessage(GetDlgItem(theDialog, IDC_EDIT_WRAP),
			EM_SETREADONLY, true, 0);
		SetWindowText(GetDlgItem(theDialog, IDC_EDIT_WRAP),
			widen(theShape.extraVarString).c_str());
		SetWindowText(GetDlgItem(theDialog, IDC_EDIT_VAR),
			widen(theShape.extraVarString).c_str());
		EnableWindow(GetDlgItem(theDialog, IDC_SPIN_WRAP), false);
		EnableWindow(GetDlgItem(theDialog, IDC_RADIO_X), false);
		EnableWindow(GetDlgItem(theDialog, IDC_RADIO_Y), false);
		CheckDlgButton(theDialog, IDC_RADIO_X,
			theShape.alignment == eAlignment_WrapY);
		CheckDlgButton(theDialog, IDC_RADIO_Y,
			theShape.alignment == eAlignment_WrapX);
		break;
	}

	return aWrapType == eWrapType_Number;
}


static void processCoordFieldChange(
	HWND theDialog,
	int theControlID,
	int theDelta = 0,
	bool theDeltaSetByMouse = false)
{
	if( !theDialog )
		return;

	LayoutEntry::Shape::Component& aComp =
		theControlID == IDC_EDIT_X	? sState->entered.x :
		theControlID == IDC_EDIT_Y	? sState->entered.y :
		theControlID == IDC_EDIT_W	? sState->entered.w :
		/*theControlID == IDC_EDIT_H*/sState->entered.h;

	if( aComp.useDefault )
		return;

	const int theBaseControlID = IDC_COMBO_X_BASE + (theControlID - IDC_EDIT_X);
	const std::string& aBaseControlStr =
		getControlText(theDialog, theBaseControlID);
	const bool isAnchorOnly = aBaseControlStr == narrow(kAnchorOnlyLabel);
	const bool isOffset = !isAnchorOnly &&
		aBaseControlStr != narrow(kValueOnlyLabel);
	const std::string& aControlStr = getControlText(theDialog, theControlID);
	size_t aStrPos = 0;
	double aNewValue = stringToDoubleSum(aControlStr, aStrPos);
	const bool isInPercent =
		isAnchorOnly &&
		aStrPos < aControlStr.size() &&
		aControlStr[aStrPos] == '%';
	if( isInPercent )
		aNewValue /= 100.0;

	if( theDelta != 0 )
	{
		const bool isY = theControlID == IDC_EDIT_Y;
		const double aWinMax = isY
			? WindowManager::overlayTargetSize().cy
			: WindowManager::overlayTargetSize().cx;
		if( isAnchorOnly )
		{// Have each point of delta count as 1 pixel of anchor
			aNewValue = (aNewValue * aWinMax + theDelta) / aWinMax;
		}
		else if( theDeltaSetByMouse )
		{// Adjust for scale and possibly auto-swap anchor type
			if( gUIScale != 1.0 )
			{
				double* aDeltaFP = isY
					? &sState->unappliedDeltaY
					: &sState->unappliedDeltaX;
				*aDeltaFP += double(theDelta) / gUIScale;
				theDelta = int(*aDeltaFP);
				*aDeltaFP -= theDelta;
			}
			aNewValue += theDelta;
			// Auto-swap basic basic named anchor type if new position is
			// very clase to a different basic named anchor point (edge/center)
			const double anAnchorPercent =
				isY && aBaseControlStr == "T"	? 0.0 :
				isY && aBaseControlStr == "CY"	? 0.5 :
				isY && aBaseControlStr == "B"	? 1.0 :
				!isY && aBaseControlStr == "L"	? 0.0 :
				!isY && aBaseControlStr == "CX"	? 0.5 :
				!isY && aBaseControlStr == "R"	? 1.0 :
				/*otherwise*/					  -1.0;
			if( anAnchorPercent >= 0.0 )
			{// Must be one of the named anchor points, can auto-swap
				const double aWinPos = clamp(
					floor(aWinMax * anAnchorPercent + 0.5) + aNewValue,
					0, aWinMax-1.0);
				double aNewAnchorPercent = anAnchorPercent;
				if( anAnchorPercent > 0.0 && aWinPos <= aWinMax * 0.02 )
				{// Swap to left/top anchor
					aNewAnchorPercent = 0;
				}
				else if( anAnchorPercent < 1.0 && aWinPos >= aWinMax * 0.98 )
				{// Swap to right/bottom anchor
					aNewAnchorPercent = 1.0;
				}
				// Could also swap to center anchor, but perhaps only if both
				// X and Y are near center point, and past testing showed this
				// to be more annoying than helpful, so left out for now.
				if( aNewAnchorPercent != anAnchorPercent )
				{
					const int aNewAnchorType =
						aNewAnchorPercent == 0.0 ? 0 :
						aNewAnchorPercent == 0.5 ? 1 :
						/*aNewAnchorPercent == 1.0*/2;		
					HWND hComboBox = GetDlgItem(theDialog, theBaseControlID);
					SendMessage(hComboBox, CB_SETCURSEL, aNewAnchorType, 0);
					SendMessage(theDialog, WM_COMMAND,
						MAKEWPARAM(theBaseControlID, CBN_SELCHANGE),
						(LPARAM)hComboBox);
					// Above will change offset, base string, etc, so need to
					// restart processing of coordinate string from the top
					// (treating theDelta as direct rather than mouse-based
					// since have already adjusted it for that here).
					processCoordFieldChange(theDialog, theControlID, theDelta);
					return;
				}
			}
		}
		else
		{// Just directly shift the offset by requested amount (spinner)
			aNewValue += theDelta;
		}
	}

	if( theControlID == IDC_EDIT_W || theControlID == IDC_EDIT_H )
	{// Do not allow total value of the component to be < 1
		aStrPos = 0;
		const double aBaseValue = !isOffset ? 0.0 :
			stringToDoubleSum(Profile::expandVars(aBaseControlStr), aStrPos);
		if( floor(aBaseValue) + floor(aNewValue) < 1.0 )
			aNewValue = ceil(1.0 - floor(aBaseValue));
	}

	std::string aFormattedString;
	if( isAnchorOnly )
	{// Allow decimals/percent in final string
		DBG_ASSERT(theControlID == IDC_EDIT_X || theControlID == IDC_EDIT_Y);
		aNewValue = clamp(aNewValue, 0.0, 1.0);
		aFormattedString = numToStringDecimal(aNewValue, isInPercent);
	}
	else
	{// Only allow integer values in final string
		aNewValue = clamp(floor(aNewValue + 0.5), -0x8000, 0x7FFF);
		aFormattedString =
			(aNewValue >= 0 && isOffset ? "+" : "") +
			toString(int(aNewValue));
	}
	if( aFormattedString != aControlStr )
	{
		SetWindowText(
			GetDlgItem(theDialog, theControlID),
			widen(aFormattedString).c_str());
	}

	// Now actually apply the string to the component
	// For anchor-only apply to .base string, otherwise offset int
	if( isAnchorOnly )
	{
		if( aComp.base != aFormattedString )
		{
			aComp.base = aFormattedString;
			sState->needsDrawPosUpdate = true;
			gRefreshOverlays.set(kSystemOverlayID);
		}
	}
	else if( aComp.offset != int(aNewValue) )
	{
		aComp.offset = int(aNewValue);
		sState->needsDrawPosUpdate = true;
		gRefreshOverlays.set(kSystemOverlayID);
	}
}


static INT_PTR CALLBACK editLayoutToolbarProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];
	const LayoutEntry* aSrcEntry = null;

	switch(theMessage)
	{
	case WM_INITDIALOG:
		// Set title to match the entry name and display scaling being applied
		{
			double aScaling = 1.0;
			if( entryIsHotspot(anEntry) && anEntry.hotpotSetEntryIdx > 0 )
			{
				aScaling = stringToFloat(Profile::expandVars(
					sState->entries[anEntry.hotpotSetEntryIdx].shape.scale));
				if( aScaling == 0 )
					aScaling = 1.0;
			}
			aScaling *= gUIScale;
			if( aScaling != 1 )
			{
				SetWindowText(theDialog,
					(L"Repositioning: " +
					 widen(anEntry.item.name) +
					 L" (at " +
					 widen(numToStringDecimal(aScaling, true)) +
					 L" scale)").c_str());
			}
			else
			{
				SetWindowText(theDialog,
					(L"Repositioning: " +
					 widen(anEntry.item.name)).c_str());
			}
		}
		// Allow Esc to cancel even when not the active window
		RegisterHotKey(NULL, kCancelToolbarHotkeyID, 0, VK_ESCAPE);
		
		// Populate controls with initial values from active entry
		// Position (X & Y)
		aSrcEntry = getPosSourceEntry(&anEntry);
		setupPositionControls(theDialog, *aSrcEntry, aSrcEntry->shape.x, false);
		setupPositionControls(theDialog, *aSrcEntry, aSrcEntry->shape.y, true);
		if( aSrcEntry != &anEntry )
		{
			DBG_ASSERT(entryIsMenu(anEntry));
			SendMessage(GetDlgItem(theDialog, IDC_EDIT_X),
				EM_SETREADONLY, true, 0);
			EnableWindow(GetDlgItem(theDialog, IDC_COMBO_X_BASE), false);
			EnableWindow(GetDlgItem(theDialog, IDC_SPIN_X), false);
			SendMessage(GetDlgItem(theDialog, IDC_EDIT_Y),
				EM_SETREADONLY, true, 0);
			EnableWindow(GetDlgItem(theDialog, IDC_COMBO_Y_BASE), false);
			EnableWindow(GetDlgItem(theDialog, IDC_SPIN_Y), false);
		}
		else if( entryIsMenu(anEntry) )
		{
			CheckDlgButton(theDialog, IDC_CHECK_POSITION, true);
		}
		
		// Size (W & H)
		if( !entryIsRangeOffsets(anEntry) )
		{
			aSrcEntry = getSizeSourceEntry(&anEntry);
			setupSizeControls(theDialog, aSrcEntry->shape.w, false);
			setupSizeControls(theDialog, aSrcEntry->shape.h, true);
			if( aSrcEntry != &anEntry || aSrcEntry->shape.w.useDefault )
			{
				SendMessage(GetDlgItem(theDialog, IDC_EDIT_W),
					EM_SETREADONLY, true, 0);
				EnableWindow(GetDlgItem(theDialog, IDC_COMBO_W_BASE), false);
				EnableWindow(GetDlgItem(theDialog, IDC_SPIN_W), false);
				SendMessage(GetDlgItem(theDialog, IDC_EDIT_H),
					EM_SETREADONLY, true, 0);
				EnableWindow(GetDlgItem(theDialog, IDC_COMBO_H_BASE), false);
				EnableWindow(GetDlgItem(theDialog, IDC_SPIN_H), false);
			}
			else
			{
				CheckDlgButton(theDialog, IDC_CHECK_SIZE, true);
			}
		}

		if( entryIsMenu(anEntry) )
		{// Alignment
			aSrcEntry = getAlignmentSourceEntry(&anEntry);
			if( aSrcEntry != &anEntry ||
				aSrcEntry->shape.alignment == eAlignment_UseDefault )
			{
				EnableWindow(GetDlgItem(theDialog, IDC_COMBO_ALIGN), false);
			}
			else
			{
				CheckDlgButton(theDialog, IDC_CHECK_ALIGNMENT, true);
			}
			setupAlignmentControls(theDialog, aSrcEntry->shape);
		}

		if( entryIsHotspotSetAnchor(anEntry) )
		{// Scale
			SendDlgItemMessage(theDialog, IDC_SLIDER_S,
				TBM_SETRANGE, TRUE,
				MAKELPARAM(kMinOffsetScale, kMaxOffsetScale));
			if( setupScaleControls(theDialog, anEntry.shape.scale) )
				CheckDlgButton(theDialog, IDC_CHECK_SCALE, true);
		}

		if( entryIsRangeOffsets(anEntry) )
		{// Range wrap
			if( setupRangeWrapControls(theDialog, anEntry.shape) )
				CheckDlgButton(theDialog, IDC_CHECK_WRAP, true);
		}

		// Adjust spinner control positioning/size slightly
		for( int i = IDC_SPIN_X; i <= IDC_SPIN_WRAP; ++i)
		{
			if( HWND hCntrl = GetDlgItem(theDialog, i) )
			{
				std::pair<POINT, SIZE> aCtrlPos =
					getControlPos(theDialog, hCntrl);
				aCtrlPos.first.y += 2;
				aCtrlPos.second.cy -= 2;
				SetWindowPos(hCntrl, NULL, aCtrlPos.first.x, aCtrlPos.first.y,
					aCtrlPos.second.cx, aCtrlPos.second.cy, SWP_NOZORDER);
			}
		}

		setInitialToolbarPos(theDialog, anEntry);
		break;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDOK:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				if( HWND hFocus = GetFocus() )
				{
					switch(GetDlgCtrlID(hFocus))
					{
					case IDC_EDIT_X: case IDC_EDIT_Y:
					case IDC_EDIT_W: case IDC_EDIT_H: case IDC_EDIT_S:
						// Set focus to OK button but don't click it yet
						// (so EN_KILLFOCUS applies changes in the field)
						PostMessage(theDialog, WM_NEXTDLGCTL,
							(WPARAM)GetDlgItem(theDialog, IDOK), TRUE);
						return (INT_PTR)TRUE;
					default:
						saveNewEntryLayout();
						promptForEditEntry();
						return (INT_PTR)TRUE;
					}
				}
			}
			break;

		case IDCANCEL:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				cancelUnappliedChanges();
				promptForEditEntry();
				return (INT_PTR)TRUE;
			}
			break;

		case IDC_EDIT_X: case IDC_EDIT_Y:
		case IDC_EDIT_W: case IDC_EDIT_H:
			if( HIWORD(wParam) == EN_KILLFOCUS )
			{
				processCoordFieldChange(theDialog, LOWORD(wParam));
				applyNewLayoutProperties();
			}
			break;

		case IDC_EDIT_S:
			if( HIWORD(wParam) == EN_KILLFOCUS &&
				IsDlgButtonChecked(theDialog, IDC_CHECK_SCALE) == BST_CHECKED )
			{
				const std::string& aControlStr =
					getControlText(theDialog, IDC_EDIT_S);
				size_t aStrPos = 0;
				double aNewValue = stringToDoubleSum(aControlStr, aStrPos);
				const bool isInPercent =
					aStrPos < aControlStr.size() && aControlStr[aStrPos] == '%';
				if( isInPercent )
					aNewValue /= 100.0;
				if( aNewValue < 0.01 )
					aNewValue = 0.01;
				if( aControlStr != sState->entered.scale )
				{
					sState->entered.scale =
						numToStringDecimal(aNewValue, isInPercent);
					// Update slider to match
					SendDlgItemMessage(theDialog, IDC_SLIDER_S,
						TBM_SETPOS, TRUE,
						LPARAM(aNewValue * 100.0));
					if( aControlStr != sState->entered.scale )
					{
						SetWindowText(
							GetDlgItem(theDialog, IDC_EDIT_S),
							widen(sState->entered.scale).c_str());
					}
					sState->needsDrawPosUpdate = true;
					gRefreshOverlays.set(kSystemOverlayID);
					applyNewLayoutProperties();
				}
			}
			break;

		case IDC_EDIT_WRAP:
			if( HIWORD(wParam) == EN_KILLFOCUS &&
				IsDlgButtonChecked(theDialog, IDC_CHECK_WRAP) == BST_CHECKED )
			{
				const std::string& aControlStr =
					getControlText(theDialog, IDC_EDIT_WRAP);
				size_t aStrPos = 0;
				int aNewValue = clamp(
					int(stringToDoubleSum(aControlStr, aStrPos)),
					0, 255);
				if( aNewValue != sState->entered.wrap )
				{
					sState->entered.wrap = aNewValue;
					if( sState->entered.wrap == 0 )
						sState->entered.extraVarString = "0 (Off)";
					else
						sState->entered.extraVarString = toString(aNewValue);
					if( aControlStr != sState->entered.extraVarString )
					{
						SetWindowText(
							GetDlgItem(theDialog, IDC_EDIT_WRAP),
							widen(sState->entered.extraVarString).c_str());
					}
					sState->entered.extraVarString.clear();
					sState->needsDrawPosUpdate = true;
					gRefreshOverlays.set(kSystemOverlayID);
					applyNewLayoutProperties();
				}
			}
			break;

		case IDC_CHECK_POSITION:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				const bool isChecked =
					IsDlgButtonChecked(theDialog, IDC_CHECK_POSITION)
						== BST_CHECKED;
				SendMessage(GetDlgItem(theDialog, IDC_EDIT_X),
					EM_SETREADONLY, !isChecked, 0);
				EnableWindow(GetDlgItem(theDialog, IDC_COMBO_X_BASE),isChecked);
				EnableWindow(GetDlgItem(theDialog, IDC_SPIN_X), isChecked);
				SendMessage(GetDlgItem(theDialog, IDC_EDIT_Y),
					EM_SETREADONLY, !isChecked, 0);
				EnableWindow(GetDlgItem(theDialog, IDC_COMBO_Y_BASE),isChecked);
				EnableWindow(GetDlgItem(theDialog, IDC_SPIN_Y), isChecked);
				const LayoutEntry* aSrcEntry;
				if( isChecked )
				{// Begin using own custom values (or duplicate of default's)
					aSrcEntry = getPosSourceEntry(&anEntry);
					sState->entered.x = aSrcEntry->shape.x;
					sState->entered.y = aSrcEntry->shape.y;
					if( sState->entered.x.useDefault ||
						sState->entered.y.useDefault )
					{// Don't defer to defaults while this is checked!
						sState->entered.x.useDefault = false;
						sState->entered.y.useDefault = false;
					}
				}
				else
				{// Set to defer to default values
					sState->entered.x = LayoutEntry::Shape::Component();
					sState->entered.y = LayoutEntry::Shape::Component();
					aSrcEntry = getPosSourceEntry(
						anEntry.parentID != 0
							? &sState->entries[anEntry.parentID]
							: &anEntry);
				}
				DBG_ASSERT(aSrcEntry);
				setupPositionControls(theDialog, *aSrcEntry,
					sState->entered.x.useDefault && aSrcEntry != &anEntry
						? aSrcEntry->shape.x : sState->entered.x,
						false);
				setupPositionControls(theDialog, *aSrcEntry,
					sState->entered.y.useDefault && aSrcEntry != &anEntry
						? aSrcEntry->shape.y : sState->entered.y,
						true);
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
				applyNewLayoutProperties();
			}
			break;

		case IDC_CHECK_SIZE:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				const bool isChecked =
					IsDlgButtonChecked(theDialog, IDC_CHECK_SIZE)
						== BST_CHECKED;
				SendMessage(GetDlgItem(theDialog, IDC_EDIT_W),
					EM_SETREADONLY, !isChecked, 0);
				EnableWindow(GetDlgItem(theDialog, IDC_COMBO_W_BASE),isChecked);
				EnableWindow(GetDlgItem(theDialog, IDC_SPIN_W), isChecked);
				SendMessage(GetDlgItem(theDialog, IDC_EDIT_H),
					EM_SETREADONLY, !isChecked, 0);
				EnableWindow(GetDlgItem(theDialog, IDC_COMBO_H_BASE),isChecked);
				EnableWindow(GetDlgItem(theDialog, IDC_SPIN_H), isChecked);
				const LayoutEntry* aSrcEntry;
				if( isChecked )
				{// Begin using own custom values (or duplicate of default's)
					aSrcEntry = getSizeSourceEntry(&anEntry);
					sState->entered.w = aSrcEntry->shape.w;
					sState->entered.h = aSrcEntry->shape.h;
					if( sState->entered.w.useDefault ||
						sState->entered.h.useDefault )
					{// Don't defer to defaults while this is checked!
						sState->entered.w.useDefault = false;
						sState->entered.h.useDefault = false;
						sState->entered.w.offset = 1;
						sState->entered.h.offset = 1;
					}
				}
				else
				{// Set to defer to default values
					sState->entered.w = LayoutEntry::Shape::Component();
					sState->entered.h = LayoutEntry::Shape::Component();
					aSrcEntry = getSizeSourceEntry(
						anEntry.parentID != 0
							? &sState->entries[anEntry.parentID]
							: &anEntry);
				}
				DBG_ASSERT(aSrcEntry);
				setupSizeControls(theDialog,
					sState->entered.w.useDefault && aSrcEntry != &anEntry
						? aSrcEntry->shape.w : sState->entered.w,
						false);
				setupSizeControls(theDialog,
					sState->entered.h.useDefault && aSrcEntry != &anEntry
						? aSrcEntry->shape.h : sState->entered.h,
						true);
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
				applyNewLayoutProperties();
			}
			break;

		case IDC_CHECK_ALIGNMENT:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				const bool isChecked =
					IsDlgButtonChecked(theDialog, IDC_CHECK_ALIGNMENT)
						== BST_CHECKED;
				EnableWindow(GetDlgItem(theDialog, IDC_COMBO_ALIGN), isChecked);
				const LayoutEntry* aSrcEntry;
				if( isChecked )
				{// Begin using own custom values (or duplicate of parent's)
					aSrcEntry = getAlignmentSourceEntry(&anEntry);
					sState->entered.alignment = aSrcEntry->shape.alignment;
					sState->entered.extraVarString =
						aSrcEntry->shape.extraVarString;
					if( sState->entered.alignment == eAlignment_UseDefault )
					{// Don't defer to defaults while this is checked!
						sState->entered.alignment = eAlignment_L_T;
					}
				}
				else
				{// Set to defer to default values
					sState->entered.alignment = eAlignment_UseDefault;
					sState->entered.extraVarString.clear();
					aSrcEntry = getAlignmentSourceEntry(
						anEntry.parentID > 0
							? &sState->entries[anEntry.parentID]
							: &anEntry);
				}
				DBG_ASSERT(aSrcEntry);
				setupAlignmentControls(theDialog,
					sState->entered.alignment == eAlignment_UseDefault &&
					aSrcEntry != &anEntry ? aSrcEntry->shape : sState->entered);
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
				applyNewLayoutProperties();
			}
			break;

		case IDC_CHECK_SCALE:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				const bool isChecked =
					IsDlgButtonChecked(theDialog, IDC_CHECK_SCALE)
						== BST_CHECKED;
				if( isChecked )
				{
					const std::string& aSrcStr = anEntry.shape.scale;
					const bool isInPercent =
						!aSrcStr.empty() &&
						aSrcStr[aSrcStr.size()-1] == '%';
					float aNewScaleVal = stringToFloat(
						Profile::expandVars(aSrcStr));
					if( aNewScaleVal <= 0 )
						aNewScaleVal = 1.0f;
					sState->entered.scale =
						numToStringDecimal(aNewScaleVal, isInPercent);
				}
				else
				{
					sState->entered.scale =
						getControlText(theDialog, IDC_EDIT_VAR);
				}
				setupScaleControls(theDialog, sState->entered.scale);
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
				applyNewLayoutProperties();
				break;
			}
			break;

		case IDC_CHECK_WRAP:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				const bool isChecked =
					IsDlgButtonChecked(theDialog, IDC_CHECK_WRAP)
						== BST_CHECKED;
				if( isChecked )
				{
					sState->entered.wrap = clamp(stringToInt(
						Profile::expandVars(sState->entered.extraVarString)),
						0, 255);
					if( sState->entered.wrap <= 0 )
						sState->entered.wrap = max(2, anEntry.rangeCount / 2);
					if( sState->entered.alignment != eAlignment_WrapX &&
						sState->entered.alignment != eAlignment_WrapY )
					{
						sState->entered.alignment = eAlignment_WrapY;
					}
					sState->entered.extraVarString.clear();
				}
				else
				{
					sState->entered.extraVarString =
						getControlText(theDialog, IDC_EDIT_VAR);
					sState->entered.wrap = clamp(stringToInt(
						Profile::expandVars(sState->entered.extraVarString)),
						0, 255);
					sState->entered.alignment = anEntry.shape.alignment;
				}
				setupRangeWrapControls(theDialog, sState->entered);
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
				applyNewLayoutProperties();
				break;
			}
			break;

		case IDC_RADIO_X:
		case IDC_RADIO_Y:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				sState->entered.alignment =
					LOWORD(wParam) == IDC_RADIO_X
						? eAlignment_WrapY : eAlignment_WrapX;
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
				applyNewLayoutProperties();
			}
			break;

		case IDC_COMBO_ALIGN:
			if( HIWORD(wParam) == CBN_SELCHANGE && sState )
			{
				sState->entered.alignment = EAlignment(SendMessage(
					GetDlgItem(theDialog, IDC_COMBO_ALIGN),
					CB_GETCURSEL, 0, 0));
				applyNewLayoutProperties();
			}
			break;

		case IDC_COMBO_X_BASE:
		case IDC_COMBO_Y_BASE:
			if( HIWORD(wParam) == CBN_SELCHANGE && sState )
			{
				const bool isY = LOWORD(wParam) == IDC_COMBO_Y_BASE;
				LayoutEntry::Shape::Component& aComp =
					isY ? sState->entered.y : sState->entered.x;
				const std::string& aSelectedText =
					getControlText(theDialog, LOWORD(wParam));
				// Borrow the draw hotspot so actual on-screen position is
				// maintained as swap to a different base (anchor) position
 				updateDrawHotspot(anEntry, sState->entered, false, false);
				const int aWinMax = isY
					? WindowManager::overlayTargetSize().cy
					: WindowManager::overlayTargetSize().cx;
				const int aDrawnPos = (isY
					? u16ToRangeVal(anEntry.drawHotspot.y.anchor, aWinMax)
					: u16ToRangeVal(anEntry.drawHotspot.x.anchor, aWinMax))
					+ (isY	? int(anEntry.drawHotspot.y.offset * gUIScale)
							: int(anEntry.drawHotspot.x.offset * gUIScale));
				if( aSelectedText == narrow(kAnchorOnlyLabel) )
				{
					// Convert drawn pos to a percentage of window size
					const double aPercent =
						clamp(aDrawnPos / double(aWinMax), 0, 1.0);
					aComp.base = numToStringDecimal(aPercent, true);
					aComp.offset = 0;
					SetWindowText(
						GetDlgItem(theDialog, isY ? IDC_EDIT_Y : IDC_EDIT_X),
						widen(aComp.base).c_str());
				}
				else
				{
					aComp.base = aSelectedText;
					// Treat as having no base if just acting as an offset, or
					// for menus or no-parent hotspots, don't bother having a
					// base string for L/T as those are implied (for hotspots
					// with parents, L/T being included indicates parent should
					// be ignored and to not act as an offset, so keep them).
					if( aSelectedText == narrow(kOffsetOnlyLabel) ||
						((entryIsMenu(anEntry) || !anEntry.parentID) &&
						 (aSelectedText == "L" || aSelectedText == "T")) )
					{
						aComp.base.clear();
					}
					aComp.offset = 0;
					// Set offset to delta from old drawn pos to new base
					updateDrawHotspot(anEntry, sState->entered, false, false);
					const int aBaseDrawnPos = (isY
						? u16ToRangeVal(anEntry.drawHotspot.y.anchor, aWinMax)
						: u16ToRangeVal(anEntry.drawHotspot.x.anchor, aWinMax))
						+ (isY	? int(anEntry.drawHotspot.y.offset * gUIScale)
								: int(anEntry.drawHotspot.x.offset * gUIScale));
					aComp.offset = aDrawnPos - aBaseDrawnPos;
					if( gUIScale != 1.0 )
					{
						aComp.offset = aComp.offset >= 0
							? int(ceil(aComp.offset / gUIScale))
							: int(floor(aComp.offset / gUIScale));
					}
					SetWindowText(
						GetDlgItem(theDialog, isY ? IDC_EDIT_Y : IDC_EDIT_X),
						((aComp.offset >= 0 ? L"+" : L"") +
						 widen(toString(aComp.offset))).c_str());
				}
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
				applyNewLayoutProperties();
			}
			break;

		case IDC_COMBO_W_BASE:
		case IDC_COMBO_H_BASE:
			if( HIWORD(wParam) == CBN_SELCHANGE && sState )
			{
				const bool isH = LOWORD(wParam) == IDC_COMBO_H_BASE;
				LayoutEntry::Shape::Component& aComp =
					isH ? sState->entered.h : sState->entered.w;
				const std::string& aSelectedText =
					getControlText(theDialog, LOWORD(wParam));
				const std::string& anOffsetText =
					getControlText(theDialog, isH ? IDC_EDIT_H : IDC_EDIT_W);
				const bool useDirectValue =
					aSelectedText == narrow(kValueOnlyLabel);
				const bool useStringBase = !useDirectValue;
				const int aPrevOffset = anOffsetText.empty()
					? aComp.offset : stringToInt(anOffsetText);
				const int aPrevFullValue = max(1, aPrevOffset +
					stringToInt(Profile::expandVars(aComp.base)));
				const int aNewBaseValue = useStringBase
					? stringToInt(Profile::expandVars(aSelectedText)) : 0;
				if( useStringBase )
					aComp.base = aSelectedText;
				else
					aComp.base.clear();
				aComp.offset = aPrevFullValue - aNewBaseValue;
				SetWindowText(
					GetDlgItem(theDialog, isH ? IDC_EDIT_H : IDC_EDIT_W),
					((useStringBase && aComp.offset >= 0 ? L"+" : L"") +
					widen(toString(aComp.offset))).c_str());
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
				applyNewLayoutProperties();
			}
			break;
		}
		break;

	case WM_HSCROLL:
		if( (HWND)lParam == GetDlgItem(theDialog, IDC_SLIDER_S) )
		{
			const int aScaleVal = dropTo<int>(
				SendDlgItemMessage(theDialog, IDC_SLIDER_S, TBM_GETPOS, 0, 0));
			const bool isInPercent =
				!sState->entered.scale.empty() &&
				sState->entered.scale[sState->entered.scale.size()-1] == '%';
			sState->entered.scale = numToStringDecimal(aScaleVal, isInPercent);
			SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S),
				widen(sState->entered.scale).c_str());
			sState->needsDrawPosUpdate = true;
			gRefreshOverlays.set(kSystemOverlayID);
			applyNewLayoutProperties();
		}
		break;

	case WM_NOTIFY:
		switch(((LPNMHDR)lParam)->idFrom)
		{
		case IDC_SPIN_X: case IDC_SPIN_W: case IDC_SPIN_H:
			processCoordFieldChange(theDialog,
				IDC_EDIT_X +
				dropTo<int>(((LPNMHDR)lParam)->idFrom) - IDC_SPIN_X,
				-((LPNMUPDOWN)lParam)->iDelta);
			applyNewLayoutProperties();
			break;
		case IDC_SPIN_Y:
			processCoordFieldChange(theDialog,
				IDC_EDIT_X +
				dropTo<int>(((LPNMHDR)lParam)->idFrom) - IDC_SPIN_X,
				((LPNMUPDOWN)lParam)->iDelta);
			applyNewLayoutProperties();
			break;
		case IDC_SPIN_WRAP:
			sState->entered.wrap = stringToInt(
				Profile::expandVars(getControlText(
					theDialog, IDC_EDIT_WRAP)));
			sState->entered.wrap -= ((LPNMUPDOWN)lParam)->iDelta;
			sState->entered.wrap = clamp(sState->entered.wrap, 0, 255);
			if( sState->entered.wrap == 0 )
			{
				SetWindowText(GetDlgItem(theDialog, IDC_EDIT_WRAP),
					L"0 (Off)");
			}
			else
			{
				SetWindowText(GetDlgItem(theDialog, IDC_EDIT_WRAP),
					widen(toString(sState->entered.wrap)).c_str());
			}
			sState->entered.extraVarString.clear();
			sState->needsDrawPosUpdate = true;
			gRefreshOverlays.set(kSystemOverlayID);
			applyNewLayoutProperties();
			break;
		}
		break;

	case WM_DESTROY:
		UnregisterHotKey(NULL, kCancelToolbarHotkeyID);
		break;
	}

	return (INT_PTR)FALSE;
}


static INT_PTR CALLBACK editUIScaleToolbarProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	switch(theMessage)
	{
	case WM_INITDIALOG:
		WindowManager::loadProfileChanges(); // to refresh gUIScale
		SendDlgItemMessage(theDialog, IDC_SLIDER_S,
			TBM_SETRANGE, TRUE,
			MAKELPARAM(kMinOffsetScale, kMaxOffsetScale));
		SendDlgItemMessage(theDialog, IDC_SLIDER_S,
			TBM_SETPOS, TRUE, LPARAM(100 * gUIScale));
		SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S),
			widen(sState->entered.scale).c_str());
		{
			size_t aPos = 0;
			SetWindowText(theDialog,
				(L"Set Global UI Scale Multiplier (was " +
				 widen(numToStringDecimal(stringToDoubleSum(
					Profile::expandVars(sState->entered.scale), aPos), true)) +
				 L")").c_str());
		}
		// Allow Esc to cancel even when not the active window
		RegisterHotKey(NULL, kCancelToolbarHotkeyID, 0, VK_ESCAPE);
		break;
		
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDOK:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				if( HWND hFocus = GetFocus() )
				{
					switch(GetDlgCtrlID(hFocus))
					{
					case IDC_EDIT_S:
						// Set focus to OK button but don't click it yet
						// (so EN_KILLFOCUS applies changes in the field)
						PostMessage(theDialog, WM_NEXTDLGCTL,
							(WPARAM)GetDlgItem(theDialog, IDOK), TRUE);
						return (INT_PTR)TRUE;
					default:
						saveNewEntryLayout();
						promptForEditEntry();
						return (INT_PTR)TRUE;
					}
				}
			}
			break;

		case IDCANCEL:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				cancelUnappliedChanges();
				promptForEditEntry();
				return (INT_PTR)TRUE;
			}
			break;

		case IDC_EDIT_S:
			if( HIWORD(wParam) == EN_CHANGE )
			{
				std::string aNewScaleStr =
					getControlText(theDialog, IDC_EDIT_S);
				if( aNewScaleStr != sState->entered.scale )
				{
					sState->entered.scale = aNewScaleStr;
					aNewScaleStr = Profile::expandVars(sState->entered.scale);
					size_t aPos = 0;
					double aNewScaleVal = stringToDoubleSum(
						Profile::expandVars(aNewScaleStr), aPos);
					if( aPos < aNewScaleStr.size() )
						aNewScaleVal = stringToDouble(aNewScaleStr, true);
					if( !_isnan(aNewScaleVal) &&
						aNewScaleVal >= 0.01 &&
						aNewScaleVal <= 10.0 )
					{
						SendDlgItemMessage(theDialog, IDC_SLIDER_S,
							TBM_SETPOS, TRUE, LPARAM(100 * aNewScaleVal));
						applyNewLayoutProperties();
					}
				}
				return (INT_PTR)TRUE;
			}
			break;

		}
		break;
		
	case WM_HSCROLL:
		if( (HWND)lParam == GetDlgItem(theDialog, IDC_SLIDER_S) )
		{
			const int aScaleVal = dropTo<int>(
				SendDlgItemMessage(theDialog, IDC_SLIDER_S, TBM_GETPOS, 0, 0));
			const bool isInPercent =
				!sState->entered.scale.empty() &&
				(sState->entered.scale.find('$') != std::string::npos || // var
				 sState->entered.scale[sState->entered.scale.size()-1] == '%');
			SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S),
				widen(numToStringDecimal(aScaleVal, isInPercent)).c_str());
		}
		break;

	case WM_DESTROY:
		UnregisterHotKey(NULL, kCancelToolbarHotkeyID);
		break;
	}

	return (INT_PTR)FALSE;
}


static LRESULT CALLBACK layoutEditorWindowProc(
	HWND theWindow, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	if( !sState )
		return DefWindowProc(theWindow, theMessage, wParam, lParam);

	bool stopDragging = false;

	switch(theMessage)
	{
	case WM_LBUTTONDOWN:
		sState->draggingWithMouse = true;
		sState->lastMouseDragPos.x = (short)LOWORD(lParam);
		sState->lastMouseDragPos.y = (short)HIWORD(lParam);
		SetCapture(theWindow);
		ShowCursor(FALSE);
		sState->needsDrawPosUpdate = true;
		gRefreshOverlays.set(kSystemOverlayID);
		if( sState->entered.x.useDefault ||
			sState->entered.y.useDefault )
		{// Unlock position controls and set to match mouse pos
			updateDrawHotspot(
				sState->entries[sState->activeEntry],
				sState->entered);
			const POINT& aDesiredPos = WindowManager::hotspotToOverlayPos(
				sState->entries[sState->activeEntry].drawHotspot);
			CheckDlgButton(
				WindowManager::toolbarHandle(), IDC_CHECK_POSITION, true);
			SendMessage(WindowManager::toolbarHandle(), WM_COMMAND,
				MAKEWPARAM(IDC_CHECK_POSITION, BN_CLICKED),
				(LPARAM)GetDlgItem(
					WindowManager::toolbarHandle(), IDC_CHECK_POSITION));
			updateDrawHotspot(
				sState->entries[sState->activeEntry],
				sState->entered);
			const POINT& aCurrPos = WindowManager::hotspotToOverlayPos(
				sState->entries[sState->activeEntry].drawHotspot);
			if( aCurrPos.x != aDesiredPos.x || aCurrPos.y != aDesiredPos.y )
			{
				processCoordFieldChange(WindowManager::toolbarHandle(),
					IDC_EDIT_X, aDesiredPos.x - aCurrPos.x, true);
				processCoordFieldChange(WindowManager::toolbarHandle(),
					IDC_EDIT_Y, aDesiredPos.y - aCurrPos.y, true);
			}
		}
		return 0;
	case WM_LBUTTONUP:
		stopDragging = true;
		// fall through to process final position change
	case WM_MOUSEMOVE:
		if( sState->draggingWithMouse )
		{
			POINT aMousePos;
			aMousePos.x = (short)LOWORD(lParam);
			aMousePos.y = (short)HIWORD(lParam);

			if( aMousePos.x != sState->lastMouseDragPos.x )
			{
				processCoordFieldChange( WindowManager::toolbarHandle(),
					IDC_EDIT_X, aMousePos.x - sState->lastMouseDragPos.x, true);
			}
			if( aMousePos.y != sState->lastMouseDragPos.y )
			{
				processCoordFieldChange(WindowManager::toolbarHandle(),
					IDC_EDIT_Y, aMousePos.y - sState->lastMouseDragPos.y, true);
			}

			sState->lastMouseDragPos = aMousePos;
			if( stopDragging )
			{
				ShowCursor(TRUE);
				ReleaseCapture();
				sState->draggingWithMouse = false;
				sState->unappliedDeltaX = sState->unappliedDeltaY = 0;
				applyNewLayoutProperties();
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
			}
		}
		return 0;
	case WM_SETCURSOR:
		if( !sState->draggingWithMouse )
		{
			SetCursor(LoadCursor(NULL, IDC_SIZEALL));
			processCoordFieldChange(WindowManager::toolbarHandle(), IDC_EDIT_X);
			processCoordFieldChange(WindowManager::toolbarHandle(), IDC_EDIT_Y);
		}
		return 0;
	}

	return DefWindowProc(theWindow, theMessage, wParam, lParam);
}


static RECT drawHotspot(
	HDC hdc,
	const Hotspot& theHotspot,
	int theExtents,
	double theHotspotSizeScale,
	const POINT& theBaseOffset,
	COLORREF theEraseColor,
	bool asActiveHotspot,
	bool includeSizeBox)
{
	const SIZE& aTargetSize = WindowManager::overlayTargetSize();
	RECT aFullDrawnRect = { 0 };

	if( includeSizeBox &&
		(theHotspot.w > theExtents * 2 || theHotspot.h > theExtents * 2) )
	{// Draw bounding box to show hotspot size
		const SIZE aBoxSize = {
			LONG(theHotspot.w * theHotspotSizeScale * gUIScale + 0.5),
			LONG(theHotspot.h * theHotspotSizeScale * gUIScale + 0.5) };
		RECT aRect;
		aRect.left = LONG(u16ToRangeVal(theHotspot.x.anchor, aTargetSize.cx)) +
			LONG(theHotspot.x.offset * gUIScale) +
				theBaseOffset.x - aBoxSize.cx / 2;
		aRect.right = aRect.left + aBoxSize.cx;
		aRect.top = LONG(u16ToRangeVal(theHotspot.y.anchor, aTargetSize.cy)) +
			LONG(theHotspot.y.offset * gUIScale) +
				theBaseOffset.y - aBoxSize.cy / 2;
		aRect.bottom = aRect.top + aBoxSize.cy;

		// Draw outer-most black border
		COLORREF oldBrushColor = SetDCBrushColor(hdc, theEraseColor);
		HBRUSH hOldBrush = (HBRUSH)SelectObject(
			hdc, GetStockObject(NULL_BRUSH));
		InflateRect(&aRect, 2, 2);
		Rectangle(hdc, aRect.left, aRect.top, aRect.right, aRect.bottom);
		UnionRect(&aFullDrawnRect, &aFullDrawnRect, &aRect);
		
		// Draw white/grey border just outside of (undrawn) inner rect
		InflateRect(&aRect, -1, -1);
		HPEN hOldPen = (HPEN)SelectObject(hdc, GetStockObject(DC_PEN));
		COLORREF oldPenColor = SetDCPenColor(hdc, oldBrushColor);
		Rectangle(hdc, aRect.left, aRect.top, aRect.right, aRect.bottom);
		SetDCPenColor(hdc, oldPenColor);
		SelectObject(hdc, hOldPen);
		SelectObject(hdc, hOldBrush);
		InflateRect(&aRect, -1, -1);

		// Erase contents of inner rect for active hotspot, for further clarity
		if( asActiveHotspot )
			FillRect(hdc, &aRect, hOldBrush);
		SetDCBrushColor(hdc, oldBrushColor);

		// For non-active hotspots, don't draw the point itself, just the box
		if( !asActiveHotspot )
			return aFullDrawnRect;
	}

	const POINT aCenterPoint = {
		LONG(u16ToRangeVal(theHotspot.x.anchor, aTargetSize.cx)) +
			LONG(theHotspot.x.offset * gUIScale) + theBaseOffset.x,
		LONG(u16ToRangeVal(theHotspot.y.anchor, aTargetSize.cy)) +
			LONG(theHotspot.y.offset * gUIScale) + theBaseOffset.y };
	if( asActiveHotspot )
	{// Draw extended crosshair arms
		Rectangle(hdc,
			aCenterPoint.x - theExtents * 3,
			aCenterPoint.y - 1,
			aCenterPoint.x + theExtents * 3 + 1,
			aCenterPoint.y + 2);
		Rectangle(hdc,
			aCenterPoint.x - 1,
			aCenterPoint.y - theExtents * 3,
			aCenterPoint.x + 2,
			aCenterPoint.y + theExtents * 3 + 1);
	}
	// Draw main rectangle
	RECT aRect = {
		aCenterPoint.x - theExtents,
		aCenterPoint.y - theExtents,
		aCenterPoint.x + theExtents + 1,
		aCenterPoint.y + theExtents + 1 };
	Rectangle(hdc,
		aRect.left, aRect.top,
		aRect.right, aRect.bottom);
	if( asActiveHotspot )
	{
		if( sState && sState->draggingWithMouse )
		{// Erase center dot for extra help with positioning
			RECT aCenterDot;
			const int aDotExtents = 1;
			aCenterDot.left = aCenterPoint.x - aDotExtents;
			aCenterDot.top = aCenterPoint.y - aDotExtents;
			aCenterDot.right = aCenterPoint.x + aDotExtents + 1;
			aCenterDot.bottom = aCenterPoint.y + aDotExtents + 1;
			COLORREF oldColor = SetDCBrushColor(hdc, theEraseColor);
			HBRUSH hBrush = (HBRUSH)GetCurrentObject(hdc, OBJ_BRUSH);
			FillRect(hdc, &aCenterDot, hBrush);
			SetDCBrushColor(hdc, oldColor);
		}
		// Extend known drawn rect by earlier crosshair size
		InflateRect(&aRect, theExtents * 2, theExtents * 2);
	}

	UnionRect(&aFullDrawnRect, &aFullDrawnRect, &aRect);
	return aFullDrawnRect;
}


static void drawEntry(
	LayoutEntry& theEntry,
	HDC hdc,
	const POINT& theBaseOffset,
	COLORREF theEraseColor,
	bool isActiveHotspot)
{
	if( theEntry.type < LayoutEntry::eType_CategoryNum )
		return;
	if( !isActiveHotspot )
	{// Draw basic hotspot
		theEntry.drawnRect = drawHotspot(hdc, theEntry.drawHotspot,
			kChildHotspotDrawSize, theEntry.drawOffScale,
			theBaseOffset, theEraseColor, false, true);
	}
	else if( entryIsRangeOffsets(theEntry) )
	{// Draw parent hotspot (first in range) as reference point
		drawEntry( sState->entries[theEntry.parentID],
			hdc, theBaseOffset, theEraseColor, false);
	}
	for(int i = 0, end = intSize(theEntry.children.size()); i < end; ++i)
	{// Draw child hotspots
		LayoutEntry& aChildEntry = sState->entries[theEntry.children[i]];
		drawEntry(aChildEntry, hdc, theBaseOffset, theEraseColor, false);
	}
	if( isActiveHotspot )
	{// Draw primary hotspot last so is over top of all children
		// Only draw size box for active entry for normal hotspots, not
		// set anchor hotspots (size is only specified for those to pass on to
		// the rest of the set really) or menus (size is displayed by menu
		// itself being drawn).
		COLORREF oldColor = SetDCBrushColor(hdc, RGB(255, 255, 255));
		theEntry.drawnRect = drawHotspot(hdc, theEntry.drawHotspot,
			kActiveHotspotDrawSize,
			theEntry.drawOffScale,
			theBaseOffset, theEraseColor, true,
			!entryIsHotspotSetAnchor(theEntry) && !entryIsMenu(theEntry));
		SetDCBrushColor(hdc, oldColor);
	}
}


static void eraseDrawnEntry(LayoutEntry& theEntry, HDC hdc)
{
	HBRUSH hEraseBrush = (HBRUSH)GetCurrentObject(hdc, OBJ_BRUSH);
	RECT aDrawnRect = theEntry.drawnRect;
	FillRect(hdc, &aDrawnRect, hEraseBrush);
	for(int i = 0, end = intSize(theEntry.children.size()); i < end; ++i )
	{
		LayoutEntry& aChildEntry = sState->entries[theEntry.children[i]];
		eraseDrawnEntry(aChildEntry, hdc);
	}
}


static void layoutEditorPaintFunc(
	HDC hdc, const RECT& theWindowRect, bool firstDraw)
{
	if( !sState )
		return;

	LayoutEntry& anEntry = sState->entries[sState->activeEntry];
	if( sState->needsDrawPosUpdate )
	{// Need to erase previous positions and update to new ones
		if( !firstDraw )
			eraseDrawnEntry(anEntry, hdc);
		updateDrawHotspot(anEntry, sState->entered, firstDraw);
	}

	POINT aBaseOffset = { theWindowRect.left, theWindowRect.top };
	COLORREF anEraseColor = SetDCBrushColor(hdc, RGB(128, 128, 128));
	HPEN hOldPen = (HPEN)SelectObject(hdc, GetStockObject(BLACK_PEN));
	drawEntry(anEntry, hdc, aBaseOffset, anEraseColor, true);
	SelectObject(hdc, hOldPen);

	// Cleanup
	sState->needsDrawPosUpdate = false;
}


static void promptForEditEntry()
{
	DBG_ASSERT(sState);
	// Set root's parent to be last-selected item if there was one
	// This signals to the dialog which item to start out already selected
	sState->entries[0].item.parentIndex = sState->activeEntry;
	sState->activeEntry = Dialogs::layoutItemSelect(sState->dialogItems);
	DBG_ASSERT(sState->activeEntry >= 0);
	DBG_ASSERT(size_t(sState->activeEntry) < sState->entries.size());
	if( sState->activeEntry )
	{
		LayoutEntry& anEntry = sState->entries[sState->activeEntry];
		sState->entered = sState->applied = anEntry.shape;
		// Any modification of a range element changes it to a normal hotspot
		sState->entered.rangeIdx = 0;
		sState->needsDrawPosUpdate = true;
		WindowManager::setSystemOverlayCallbacks(
			layoutEditorWindowProc, layoutEditorPaintFunc);
		const bool isUIScale = anEntry.type == LayoutEntry::eType_UIScale;
		WindowManager::createToolbarWindow(
			isUIScale						? IDD_DIALOG_LAYOUT_UISCALE :
			entryIsMenu(anEntry)			? IDD_DIALOG_LAYOUT_MENU :
			entryIsHotspotSetAnchor(anEntry)? IDD_DIALOG_LAYOUT_ANCHOR :
			entryIsRangeOffsets(anEntry)	? IDD_DIALOG_LAYOUT_HOTSPOT_RANGE :
			/*otherwise*/					  IDD_DIALOG_LAYOUT_HOTSPOT,
			isUIScale ? editUIScaleToolbarProc : editLayoutToolbarProc,
			entryIsMenu(anEntry) ? anEntry.menuID : -1);
	}
	else
	{
		cleanup();
	}
}


static bool parseHotspotEntry(
	LayoutEntry& theNewEntry,
	const std::string& theDesc,
	size_t& thePos,
	const std::string& theSetName)
{
	// Check for an override anchor hotspot in format "Name:"
	const size_t anEndPos = fetchComponentStringEnd(theDesc, thePos, true);
	if( anEndPos < theDesc.size() && theDesc[anEndPos] == ':' )
	{
		const std::string& anAnchorVarName =
			trim(theDesc.substr(thePos, anEndPos-thePos));
		const std::string& anAnchorName = Profile::expandVars(anAnchorVarName);
		if( !theSetName.empty() )
		{
			const std::string& aSetAnchorName = theSetName + anAnchorName;
			if( HotspotMap::hotspotIDFromName(aSetAnchorName) )
				theNewEntry.overrideAnchorName = aSetAnchorName;
		}
		if( !anAnchorName.empty() && theNewEntry.overrideAnchorName.empty() )
		{
			if( HotspotMap::hotspotIDFromName(anAnchorName) )
				theNewEntry.overrideAnchorName = anAnchorVarName;				
		}
		if( !theNewEntry.overrideAnchorName.empty() )
			thePos = anEndPos+1;
	}

	if( isEffectivelyEmptyString(theDesc.substr(thePos)) )
		return false;

	if( !fetchShapeComponent(theNewEntry.shape.x, theDesc, thePos, true) )
		return false;

	if( thePos < theDesc.size() &&
		theDesc[thePos] != '@' && theDesc[thePos] != ':' )
	{
		if( !fetchShapeComponent(theNewEntry.shape.y, theDesc, ++thePos, true) )
			return false;
	}
	
	if( thePos < theDesc.size() &&
		theDesc[thePos] != '@' && theDesc[thePos] != ':' )
	{
		if( !fetchShapeComponent(
				theNewEntry.shape.w, theDesc, ++thePos, false) ||
			thePos == theDesc.size() ||
			theDesc[thePos] == '@' ||
			theDesc[thePos] == ':' )
		{
			theNewEntry.shape.w.useDefault = true;
			return false;
		}
		if( !fetchShapeComponent(
				theNewEntry.shape.h, theDesc, ++thePos, false) )
		{
			theNewEntry.shape.w.useDefault = true;
			theNewEntry.shape.h.useDefault = true;
			return false;
		}
		// useDefault flag must match on each component pair
		theNewEntry.shape.x.useDefault = theNewEntry.shape.y.useDefault = false;
		if( theNewEntry.shape.w.base.empty() && !theNewEntry.shape.w.offset &&
			theNewEntry.shape.w.base.empty() && !theNewEntry.shape.w.offset )
		{// Width & height being 0 is equivalent to useDefault for hotspots
			theNewEntry.shape.w.useDefault = true;
			theNewEntry.shape.h.useDefault = true;
			return true;
		}
		
		// At least one was set to > 0 - make sure both are set to >= 1
		DBG_ASSERT(!theNewEntry.shape.w.useDefault);
		DBG_ASSERT(!theNewEntry.shape.h.useDefault);
		if( theNewEntry.shape.w.base.empty() )
			theNewEntry.shape.w.offset = max(1, theNewEntry.shape.w.offset);
		if( theNewEntry.shape.h.base.empty() )
			theNewEntry.shape.h.offset = max(1, theNewEntry.shape.h.offset);
	}

	return true;
}


static bool parseHotspotRangeOffsets(
	LayoutEntry& theNewEntry,
	const std::string& theDesc,
	size_t& thePos)
{
	if( thePos >= theDesc.size() || theDesc[thePos] != ':' )
		return false;

	if( !fetchShapeComponent(theNewEntry.shape.x, theDesc, ++thePos, false) ||
		thePos >= theDesc.size() ||
		theDesc[thePos] == ':' )
	{ return false; }

	if( theDesc[thePos] == '@' )
	{
		const size_t aWrapStrEndPos =
			fetchComponentStringEnd(theDesc, ++thePos);
		theNewEntry.shape.extraVarString =
			trim(theDesc.substr(thePos, aWrapStrEndPos - thePos));
		theNewEntry.shape.wrap = clamp(stringToInt(
			Profile::expandVars(theNewEntry.shape.extraVarString)),
			0, 255);
		thePos = aWrapStrEndPos;
		theNewEntry.shape.alignment = eAlignment_WrapX;
	}

	if( !fetchShapeComponent(theNewEntry.shape.y, theDesc, ++thePos, false) )
		return false;
	
	if( thePos < theDesc.size() && theDesc[thePos] == '@' )
	{
		const size_t aWrapStrEndPos =
			fetchComponentStringEnd(theDesc, ++thePos);
		theNewEntry.shape.extraVarString =
			trim(theDesc.substr(thePos, aWrapStrEndPos - thePos));
		theNewEntry.shape.wrap = clamp(stringToInt(
			Profile::expandVars(theNewEntry.shape.extraVarString)),
			0, 255);
		thePos = aWrapStrEndPos;
		theNewEntry.shape.alignment = eAlignment_WrapY;
	}

	return true;
}


//------------------------------------------------------------------------------
// Global Functions
//------------------------------------------------------------------------------

void init()
{
	gLayoutEditorRequested = false;
	if( sState )
		cleanup();

	// Reset to initial control layors & menu setup
	Menus::cleanup();
	InputTranslator::cleanup();
	Menus::init();
	InputTranslator::loadProfile();

	TargetConfigSync::pauseMonitoring();

	// Gather information on elements that can be edited
	DBG_ASSERT(sState == null);
	sState = new EditorState();

	// Start with all the categories
	{
		LayoutEntry aCatEntry;
		aCatEntry.type = LayoutEntry::eType_Root;
		aCatEntry.item.immutable = true;
		sState->entries.push_back(aCatEntry);
		aCatEntry.type = LayoutEntry::eType_HotspotCategory;
		aCatEntry.item.name = "Hotspots";
		sState->entries.push_back(aCatEntry);
		aCatEntry.type = LayoutEntry::eType_MenuCategory;
		aCatEntry.item.name = "Menus";
		sState->entries.push_back(aCatEntry);
		aCatEntry.type = LayoutEntry::eType_UIScale;
		aCatEntry.item.name = "UI Scale";
		aCatEntry.item.immutable = false;
		aCatEntry.shape.scale =
			Profile::getSectionProperties("Variables")->valElse(
			"UIScale", Profile::Property()).pattern;
		if( aCatEntry.shape.scale.empty() )
			aCatEntry.shape.scale = Profile::getVariable("UIScale");
		sState->entries.push_back(aCatEntry);
		DBG_ASSERT(sState->entries.size() == LayoutEntry::eType_CategoryNum);
		#ifndef NDEBUG
		for(int i = 0; i < LayoutEntry::eType_CategoryNum; ++i)
			DBG_ASSERT(sState->entries[i].type == i);
		#endif
	}

	// Collect hotspot data entries
	size_t aPos;
	StringToValueMap<int> aHotspotNameToEntryMap;
	for(int aHotspotSetID = 0, aHotspotSetCnt = HotspotMap::hotspotSetCount();
		aHotspotSetID < aHotspotSetCnt; ++aHotspotSetID)
	{
		int aHotspotSetAnchorIdx = 0;
		const int aSectionID = HotspotMap::hotspotSetSectionID(aHotspotSetID);
		const std::string& aSetName = aHotspotSetID == 0 ? "" :
			HotspotMap::hotspotSetLabel(aHotspotSetID);
		if( aHotspotSetID > 0 )
		{// Make sure set anchor hotspot exists in some basic form
			aHotspotSetAnchorIdx = int(sState->entries.size());
			sState->entries.push_back(LayoutEntry());
			LayoutEntry& anEntry = sState->entries.back();
			anEntry.propSectID = aSectionID;
			anEntry.type = LayoutEntry::eType_HotspotSetAnchor;
			anEntry.hotpotSetEntryIdx = 0; // Not considered part of own set
			anEntry.item.parentIndex = LayoutEntry::eType_HotspotCategory;
			anEntry.item.name = aSetName;
		}
		aHotspotNameToEntryMap.setValue(aSetName, aHotspotSetAnchorIdx);

		// Add all hotspots in the set (plus set's Anchor and Scale)
		Profile::PropertyMapPtr aPropMap =
			Profile::getSectionProperties(aSectionID);
		for(int aPropIdx = 0; aPropIdx < aPropMap->size(); ++aPropIdx)
		{
			if( aHotspotSetID > 0 )
			{// Check for special set properties Anchor and Scale
				const std::string& aCondensedKey =
					condense(aPropMap->keys()[aPropIdx]);
				if( aCondensedKey == "ANCHOR" )
				{
					LayoutEntry aNewEntry =
						sState->entries[aHotspotSetAnchorIdx];
					const std::string& aDesc =
						aPropMap->vals()[aPropIdx].pattern.empty()
							? aPropMap->vals()[aPropIdx].str
							: aPropMap->vals()[aPropIdx].pattern;
					aPos = 0;
					if( parseHotspotEntry(aNewEntry, aDesc, aPos, "") )
						sState->entries[aHotspotSetAnchorIdx] = aNewEntry;
					continue;
				}
				if( aCondensedKey == "SCALE" )
				{
					const std::string& aDesc =
						aPropMap->vals()[aPropIdx].pattern.empty()
							? aPropMap->vals()[aPropIdx].str
							: aPropMap->vals()[aPropIdx].pattern;
					sState->entries[aHotspotSetAnchorIdx].shape.scale =
						fetchShapeScale(aDesc);
					continue;
				}
			}
			LayoutEntry aNewEntry = LayoutEntry();
			aNewEntry.propSectID = aSectionID;
			aNewEntry.type = LayoutEntry::eType_Hotspot;
			aNewEntry.hotpotSetEntryIdx = aHotspotSetAnchorIdx;	
			aNewEntry.item.parentIndex =
				aHotspotSetAnchorIdx
					? aHotspotSetAnchorIdx
					: LayoutEntry::eType_HotspotCategory;
			aPos = 0;
			const std::string& aDesc =
				aPropMap->vals()[aPropIdx].pattern.empty()
					? aPropMap->vals()[aPropIdx].str
					: aPropMap->vals()[aPropIdx].pattern;
			aNewEntry.item.name = aSetName + aPropMap->keys()[aPropIdx];
			if( !parseHotspotEntry(aNewEntry, aDesc, aPos, aSetName) )
			{
				// TODO: Mark as disabled rather than immutable so can re-enable
				aNewEntry.item.immutable = true;
			}
			if( aNewEntry.overrideAnchorName.empty() )
				aNewEntry.parentID = aHotspotSetAnchorIdx;
			sState->entries.push_back(aNewEntry);
			// Check if is a range and need sub-entries
			int aRangeStartIdx, aRangeEndIdx;
			std::string aRangeName;
			if( fetchRangeSuffix(
					aNewEntry.item.name,
					aRangeName,
					aRangeStartIdx, aRangeEndIdx) )
			{
				if( !parseHotspotRangeOffsets(aNewEntry, aDesc, aPos) )
					aNewEntry.item.immutable = true; // TODO: Disabled instead
				const int aRangeFirstEntryIdx = int(sState->entries.size())-1;
				aNewEntry.overrideAnchorName.clear();
				aNewEntry.parentID = aRangeFirstEntryIdx;
				aNewEntry.item.parentIndex = aRangeFirstEntryIdx;
				aNewEntry.item.name =
					aRangeName + toString(aRangeStartIdx+1);
				if( aRangeEndIdx > aRangeStartIdx + 1)
					aNewEntry.item.name += "-" + toString(aRangeEndIdx);
				aNewEntry.rangeCount = aRangeEndIdx - aRangeStartIdx;
				aNewEntry.shape.rangeIdx = 1;
				// Though listed as "Name1-10" etc in tree view, for linking
				// to anchor hotspots it is registered as just "Name1"
				const int aRangeOffsetsEntryIdx = int(sState->entries.size());
				aHotspotNameToEntryMap.setValue(
					aRangeName + toString(aRangeStartIdx),
					aRangeOffsetsEntryIdx);
				sState->entries[aRangeFirstEntryIdx].children
					.push_back(aRangeOffsetsEntryIdx);
				// Add the second entry in range (Name2-10) for changing offsets
				sState->entries.push_back(aNewEntry);		
				// Its name for linking is also just "Name2" not "Name2-10"
				aHotspotNameToEntryMap.setValue(
					aRangeName + toString(aRangeStartIdx),
					int(sState->entries.size())-1);
				if( aRangeEndIdx <= aRangeStartIdx + 1 )
					continue;
				// Create individual entries for rest in this range so they can
				// act as individual parents if needed or override the range.
				// Borrow shapeToHotspot to convert vars to pure offsets...
				aNewEntry.drawHotspot = shapeToHotspot(aNewEntry.shape);
				const int aStepX = aNewEntry.drawHotspot.x.offset;
				const int aStepY = aNewEntry.drawHotspot.y.offset;
				const u32 applyXEvery =
					aNewEntry.shape.alignment == eAlignment_WrapX
						? aNewEntry.shape.wrap : 0;
				const u32 applyYEvery =
					aNewEntry.shape.alignment == eAlignment_WrapY
						? aNewEntry.shape.wrap : 0;
				// These use the second entry in the range, the offset, as the
				// tree view parent, even though actual direct (anchor) parent
				// is the first hotspot in the range.
				aNewEntry.item.parentIndex = aRangeOffsetsEntryIdx;
				aNewEntry.item.immutable = true; // TEMP
				aNewEntry.rangeCount = 0;
				aNewEntry.shape = LayoutEntry::Shape();
				for(int aNameIdx = aRangeStartIdx+2, anOffsetIdx = 2;
					aNameIdx <= aRangeEndIdx; ++aNameIdx, ++anOffsetIdx)
				{
					aNewEntry.item.name = aRangeName + toString(aNameIdx);
					aNewEntry.shape.rangeIdx = anOffsetIdx;
					aNewEntry.shape.x.useDefault = false;
					aNewEntry.shape.y.useDefault = false;
					const Hotspot& anOffsets = getRangeEntryOffset(
						anOffsetIdx, aStepX, aStepY, applyXEvery, applyYEvery);
					aNewEntry.shape.x.offset = anOffsets.x.offset;
					aNewEntry.shape.y.offset = anOffsets.y.offset;
					sState->entries.push_back(aNewEntry);
					sState->entries[aRangeOffsetsEntryIdx].children
						.push_back(int(sState->entries.size())-1);
					aHotspotNameToEntryMap.setValue(
						aNewEntry.item.name,
						int(sState->entries.size())-1);
				}
			}
			else
			{
				// Not a range hotspot - just add to name-to-idx map
				aHotspotNameToEntryMap.setValue(
					aNewEntry.item.name,
					int(sState->entries.size()-1));
			}
		}
	}

	// Set override anchor parents
	for(int i = LayoutEntry::eType_CategoryNum,
		end = intSize(sState->entries.size()); i < end; ++i)
	{
		if( !sState->entries[i].parentID &&
			!sState->entries[i].overrideAnchorName.empty() )
		{
			sState->entries[i].parentID =
				aHotspotNameToEntryMap.quickFind(
					Profile::expandVars(sState->entries[i].overrideAnchorName));
			DBG_ASSERT(sState->entries[i].parentID > 0);
			DBG_ASSERT(
				size_t(sState->entries[i].parentID) < sState->entries.size());
			if( !entryIsHotspotSetAnchor(sState->entries[i]) )
			{// For tree view prefer set parent (if have one) over direct parent
				sState->entries[i].item.parentIndex =
					sState->entries[i].hotpotSetEntryIdx
						? sState->entries[i].hotpotSetEntryIdx
						: sState->entries[i].parentID;
			}
		}
	}
	aHotspotNameToEntryMap.clear(); aHotspotNameToEntryMap.trim();

	// Link parent hotspot entries to their children for drawing later
	// (range offset entries aren't considered parents of first range entry)
	for(int i = LayoutEntry::eType_CategoryNum,
		end = intSize(sState->entries.size()); i < end; ++i)
	{
		LayoutEntry& anEntry = sState->entries[i];
		if( anEntry.parentID && !anEntry.shape.rangeIdx )
			sState->entries[anEntry.parentID].children.push_back(i);
	}

	// Collect menu layout entries
	int aFirstMenuEntryIdx = intSize(sState->entries.size());
	for(int aMenuID = 0, aMenuCnt = InputMap::menuCount();
		aMenuID < aMenuCnt; ++aMenuID)
	{
		bool isMenuDefaults = false;
		switch(InputMap::menuStyle(aMenuID))
		{
		case eMenuStyle_Hotspots:
		case eMenuStyle_Highlight:
		case eMenuStyle_KBCycleLast:
		case eMenuStyle_KBCycleDefault:
		case eMenuStyle_HotspotGuide:
			// These menus are not edit-able (use hotspot data instead)
			continue; // to next menu from for loop above
		case eMenuStyle_System:
			// System menu acts as a stand-in for default menu values
			isMenuDefaults = true;
			break;
		default:
			// Don't try editing menus that use a hotspot for their position
			if( InputMap::menuOriginHotspotID(aMenuID) )
				continue;
			// Add a new entry for this menu ID
			sState->entries.push_back(LayoutEntry());
			break;
		}
		Profile::PropertyMapPtr aPropMap = null;
		LayoutEntry& aNewEntry = isMenuDefaults
			? sState->entries[LayoutEntry::eType_MenuCategory]
			: sState->entries.back();
		aNewEntry.propSectID = InputMap::menuSectionID(aMenuID);
		if( aNewEntry.propSectID == kInvalidID && isMenuDefaults )
			continue;
		if( !isMenuDefaults )
		{
			aNewEntry.type = LayoutEntry::eType_Menu;
			aNewEntry.item.parentIndex = LayoutEntry::eType_MenuCategory;
			aNewEntry.parentID = LayoutEntry::eType_MenuCategory;
			aNewEntry.item.name = InputMap::menuName(aMenuID);
		}
		aNewEntry.menuID = aMenuID;
		aPropMap = Profile::getSectionProperties(aNewEntry.propSectID);
		bool valid = true;
		if( const Profile::Property* aPosProp =
				aPropMap->find(kPositionPropName) )
		{
			const std::string& aDesc = aPosProp->pattern.empty()
				? aPosProp->str : aPosProp->pattern;
			aPos = 0;
			valid = valid && fetchShapeComponent(
				aNewEntry.shape.x, aDesc, aPos, true);
			if( aPos < aDesc.size() )
			{
				valid = valid && fetchShapeComponent(
					aNewEntry.shape.y, aDesc, ++aPos, true);
			}
		}
		if( const Profile::Property* aSizeProp =
				aPropMap->find(kItemSizePropName) )
		{
			const std::string& aDesc = aSizeProp->pattern.empty()
				? aSizeProp->str : aSizeProp->pattern;
			aPos = 0;
			valid = valid && fetchShapeComponent(
				aNewEntry.shape.w, aDesc, aPos, false);
			if( aPos < aDesc.size() )
			{
				valid = valid && fetchShapeComponent(
					aNewEntry.shape.h, aDesc, ++aPos, false);
			}
		}
		if( const Profile::Property* anAlignProp =
				aPropMap->find(kAlignmentPropName) )
		{
			if( isEffectivelyEmptyString(anAlignProp->str) )
			{// No alignment specified - use default
				aNewEntry.shape.alignment = eAlignment_UseDefault;
			}
			else if( anAlignProp->pattern.empty() )
			{// No pattern vars used - parse alignment string
				aPos = 0;
				Hotspot::Coord aCoord = stringToCoord(anAlignProp->str, aPos);
				int anAlignVal =
					aCoord.anchor < 0x4000	? eAlignment_L_T :
					aCoord.anchor > 0xC000	? eAlignment_R_T :
					/*otherwise*/			  eAlignment_CX_T;
				aCoord = stringToCoord(anAlignProp->str, ++aPos);
				anAlignVal +=
					aCoord.anchor < 0x4000	? eAlignment_L_T :
					aCoord.anchor > 0xC000	? eAlignment_L_B :
					/*otherwise*/			  eAlignment_L_CY;
				aNewEntry.shape.alignment = EAlignment(anAlignVal);
			}
			else
			{// Vars used - directly use pattern string
				aNewEntry.shape.alignment = eAlignment_VarString;
				aNewEntry.shape.extraVarString = anAlignProp->pattern;
			}
		}

		if( !aNewEntry.shape.x.useDefault ||
			!aNewEntry.shape.y.useDefault ||
			isMenuDefaults )
		{
			aNewEntry.shape.x.useDefault = false;
			aNewEntry.shape.y.useDefault = false;
		}
		if( !aNewEntry.shape.w.useDefault ||
			!aNewEntry.shape.h.useDefault ||
			isMenuDefaults )
		{// If either W or H was set, make sure both are set to >= 1
			aNewEntry.shape.w.useDefault = false;
			aNewEntry.shape.h.useDefault = false;
			if( aNewEntry.shape.w.base.empty() )
				aNewEntry.shape.w.offset = max(1, aNewEntry.shape.w.offset);
			if( aNewEntry.shape.h.base.empty() )
				aNewEntry.shape.h.offset = max(1, aNewEntry.shape.h.offset);
		}
		if( aNewEntry.shape.alignment == eAlignment_UseDefault &&
			isMenuDefaults )
		{// Default alignment if unspecified at all should be TL
			aNewEntry.shape.alignment = eAlignment_L_T;
		}
		if( !valid )
			aNewEntry.item.immutable = true; // TODO - better solution
	}

	// Link menus with their parent menus (both immediate and root)
	// item.parentIndex is used just for sorting the tree view, but
	// .parentID is used for inheriting default values from root menu
	for(int i = aFirstMenuEntryIdx,
		end = intSize(sState->entries.size()); i < end; ++i)
	{
		LayoutEntry& anEntry = sState->entries[i];
		int aParentMenuID = InputMap::parentMenuOfMenu(anEntry.menuID);
		if( aParentMenuID == anEntry.menuID )
			continue;
		int aRootMenuID = InputMap::rootMenuOfMenu(anEntry.menuID);
		for(int j = aFirstMenuEntryIdx,
			end = intSize(sState->entries.size()); j < end; ++j)
		{
			if( sState->entries[j].menuID == aParentMenuID )
				anEntry.item.parentIndex = j;
			if( sState->entries[j].menuID == aRootMenuID )
			{
				anEntry.parentID = j;
				if( sState->entries[j].item.immutable )
					anEntry.item.immutable = true; // TODO - better solution
			}
		}
	}

	// Prepare for prompt dialog
	if( sState->entries.size() <= LayoutEntry::eType_CategoryNum )
	{
		logError("Current profile has no edit-able layout items!");
		return;
	}

	sState->dialogItems.reserve(sState->entries.size());
	for(int i = 0, end = intSize(sState->entries.size()); i < end; ++i)
		sState->dialogItems.push_back(&sState->entries[i].item);

	promptForEditEntry();
}


void cleanup()
{
	if( !sState )
		return;

	if( sState->activeEntry )
		cancelUnappliedChanges();
	delete sState;
	sState = null;

	TargetConfigSync::resumeMonitoring();
}

} // LayoutEditor
