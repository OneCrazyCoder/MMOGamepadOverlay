//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "HUD.h"

#include "InputMap.h"
#include "Profile.h"

namespace HUD
{

//-----------------------------------------------------------------------------
// Config
//-----------------------------------------------------------------------------

struct Config
{
	std::wstring fontName;
	int fontSize;
	int fontWeight;
	int borderThickness;
	int macroButtonW;
	int macroButtonH;
	POINT macroButtonPos[4];
	COLORREF borderColor;
	COLORREF buttonColor;
	COLORREF labelColor;

	void load()
	{
		this->fontName = widen(Profile::getStr("HUD/Font", "Verdana"));
		this->fontSize = Profile::getInt("HUD/FontSize", 13);
		this->fontWeight = Profile::getInt("HUD/FontWeight", FW_NORMAL);
		std::vector<int> anIntVec(10, 10);
		Profile::getIntArray("HUD/MacrosXY", &anIntVec);
		const int kMacrosX = anIntVec[0];
		const int kMacrosY = anIntVec[1];
		anIntVec[0] = 244; anIntVec[1] = 40;
		Profile::getIntArray("HUD/MacroWH", &anIntVec);
		this->macroButtonW = max(1, anIntVec[0]);
		this->macroButtonH = max(1, anIntVec[1]);
		this->macroButtonPos[0].x = kMacrosX+macroButtonW/2;
		this->macroButtonPos[0].y = kMacrosY;
		this->macroButtonPos[1].x = kMacrosX;
		this->macroButtonPos[1].y = kMacrosY + macroButtonH;
		this->macroButtonPos[2].x = kMacrosX + macroButtonW;
		this->macroButtonPos[2].y = macroButtonPos[1].y;
		this->macroButtonPos[3].x = macroButtonPos[0].x;
		this->macroButtonPos[3].y = macroButtonPos[1].y + macroButtonH;
		this->borderThickness = max(0, Profile::getInt("HUD/BorderSize", 1));
		anIntVec.resize(3);
		anIntVec[0] = anIntVec[1] = anIntVec[2] = 100;
		Profile::getIntArray("HUD/BorderRGB", &anIntVec);
		this->borderColor = RGB(anIntVec[0], anIntVec[1], anIntVec[2]);
		anIntVec[0] = anIntVec[1] = anIntVec[2] = 150;
		Profile::getIntArray("HUD/ButtonRGB", &anIntVec);
		this->buttonColor = RGB(anIntVec[0], anIntVec[1], anIntVec[2]);
		anIntVec[0] = anIntVec[1] = anIntVec[2] = 10;
		Profile::getIntArray("HUD/LabelRGB", &anIntVec);
		this->labelColor = RGB(anIntVec[0], anIntVec[1], anIntVec[2]);
	}
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static Config kConfig;
static HFONT sFont = NULL;
static HBRUSH sBorderBrush = NULL;
static HBRUSH sButtonBrush = NULL;
static bool sInitialized = false;


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void init()
{
	cleanup();
	kConfig.load();

	sFont = CreateFont(
		kConfig.fontSize, // Height of the font
		0, // Width of the font
		0, // Angle of escapement
		0, // Orientation angle
		kConfig.fontWeight, // Font weight
		FALSE, // Italic
		FALSE, // Underline
		FALSE, // Strikeout
		ANSI_CHARSET, // Character set identifier
		OUT_DEFAULT_PRECIS, // Output precision
		CLIP_DEFAULT_PRECIS, // Clipping precision
		DEFAULT_QUALITY, // Output quality
		DEFAULT_PITCH | FF_DONTCARE, // Pitch and family
		kConfig.fontName.c_str() // Font face name
	);
	sBorderBrush = CreateSolidBrush(kConfig.borderColor);
	sButtonBrush = CreateSolidBrush(kConfig.buttonColor);
	sInitialized = true;
}

void cleanup()
{
	if( sFont )
		DeleteObject(sFont);
	sFont = NULL;
	if( sBorderBrush )
		DeleteObject(sBorderBrush);
	sBorderBrush = NULL;
	if( sButtonBrush )
		DeleteObject(sButtonBrush);
	sButtonBrush = NULL;
	sInitialized = false;
}


void update()
{
}


void render(HWND theWindow)
{
	// Prepare to draw
	PAINTSTRUCT aPaintStruct;
	HDC hdc = BeginPaint(theWindow, &aPaintStruct);
	if( !sInitialized )
	{
		EndPaint(theWindow, &aPaintStruct);		
		return;
	}

	if( InputMap::visibleHUDElements(gControlsModeID).test(eHUDElement_Macros) )
	{
		HFONT anOldFont = (HFONT)SelectObject(hdc, sFont);
		SetBkColor(hdc, kConfig.buttonColor);
		SetTextColor(hdc, kConfig.labelColor);

		// Draw each button
		for(int i = 0; i < 4; ++i)
		{
			RECT aButtonRect;
			aButtonRect.left = kConfig.macroButtonPos[i].x;
			aButtonRect.top = kConfig.macroButtonPos[i].y;
			aButtonRect.right = kConfig.macroButtonPos[i].x + kConfig.macroButtonW;
			aButtonRect.bottom = kConfig.macroButtonPos[i].y + kConfig.macroButtonH;
			// Border
			FillRect(hdc, &aButtonRect, sBorderBrush);
			InflateRect(&aButtonRect, -kConfig.borderThickness, -kConfig.borderThickness);
			// Interior background
			FillRect(hdc, &aButtonRect, sButtonBrush);

			const std::string& aLabel = InputMap::macroLabel(gMacroSetID, i);
			if( !aLabel.empty() )
			{// Text - word-wrapped + horizontal & vertical center justification
				const std::wstring& aLabelW = widen(aLabel);
				RECT aTextRect = aButtonRect;
				aTextRect.bottom = aTextRect.top;
				const int aTextHeight = DrawText(hdc, aLabelW.c_str(),
					-1, &aTextRect, DT_WORDBREAK | DT_CENTER | DT_CALCRECT);
				aTextRect.top += (aButtonRect.bottom - aButtonRect.top - aTextHeight) / 2;
				aTextRect.right = aButtonRect.right;
				aTextRect.bottom = aButtonRect.bottom;
				DrawText(hdc, aLabelW.c_str(), -1, &aTextRect, DT_WORDBREAK | DT_CENTER);
			}
		}

		// Clean up
		SelectObject(hdc, anOldFont);
	}

	EndPaint(theWindow, &aPaintStruct);		
}

} // HUD
