//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Central location for global classes, structs, and typedefs that are needed
	by multiple modules.
*/

struct Command : public ConstructFromZeroInitializedMemory<Command>
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
				u16 layerID;
				u16 subMenuID;
				u16 hotspotID;
				u16 keyStringIdx;
				u16 arrayIdx;
				u16 menuItemIdx;
			};
			union
			{
				u16 signalID;
				u16 menuID;
				u16 keybindArrayID;
				u16 replacementLayer;
				u16 mouseWheelMotionType;
				bool multiDirAutoRun;
			};
			u8 count;
			u8 wrap : 1;
			u8 withMouse : 1;
			u8 andClick : 1;
			u8 atStartup : 1;
			u8 swapDir : 2;
			u8 __reserved : 2;
		};
		const char* string;
		u64 compare;
	};

	bool operator==(const Command& rhs) const
	{ return type == rhs.type && compare == rhs.compare; }
};


struct Hotspot : public ConstructFromZeroInitializedMemory<Hotspot>
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
