//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Central location for global classes, structs, and typedefs that are needed
	by multiple modules.
*/

struct ZERO_INIT(Hotspot)
{
	struct Coord
	{
		u16 anchor; // normalized x/65536 percentage of area
		s16 offset; // pixel offset from .anchor (is multiplied by UIScale)
		bool operator==(const Coord& rhs) const
		{ return anchor == rhs.anchor && offset == rhs.offset; }
		bool operator!=(const Coord& rhs) const
		{ return !(*this == rhs); }
	} x, y;

	bool operator==(const Hotspot& rhs) const
	{ return x == rhs.x && y == rhs.y; }
	bool operator!=(const Hotspot& rhs) const
	{ return !(*this == rhs); }
};


struct ZERO_INIT(Command)
{
	ECommandType type;
	union
	{
		struct
		{
			union
			{
				u16 dir;
				u16 vKey;
				u16 vKeySeqID;
				u16 stringID;
				u16 subMenuID;
				u16 menuItemID;
				u16 arrayIdx;
				u16 layerID;
			};
			union
			{
				u16 hotspotID;
				u16 menuID;
				u16 keyBindID;
				u16 keybindArrayID;
				u16 variableID;
				u16 replacementLayer;
				u16 mouseWheelMotionType;
			};
			s16 count;
			u16 swapDir : 2;
			u16 wrap : 1;
			u16 withMouse : 1;
			u16 andClick : 1;
			u16 multiDirAutoRun : 1;
			u16 temporary : 1;
			u16 hasKeybindSignal : 1;
			u16 asHoldAction : 1;
			u16 forced : 1;
			u16 __padding : 6;
		};
		struct { Hotspot::Coord x, y; } hotspot;
		u64 compare;
	};

	bool operator==(const Command& rhs) const
	{ return type == rhs.type && compare == rhs.compare; }
	bool operator!=(const Command& rhs) const
	{ return type != rhs.type || compare != rhs.compare; }
};
