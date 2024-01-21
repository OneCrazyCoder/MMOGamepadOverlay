//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "HUD.h"

#include "InputMap.h" // labels
#include "OverlayWindow.h" // temp hack to force redraw()
#include "Profile.h"

namespace HUD
{

#ifdef _DEBUG
// Make the actual position of the overlay window obvious by drawing a frame
//#define DEBUG_DRAW_WINDOW_FRAME
#endif

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kNoticeStringDisplayTimePerChar = 40,
kNoticeStringMinTime = 3000,
};


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
		Profile::getIntArray("HUD/MacrosXY", anIntVec);
		const int kMacrosX = anIntVec[0];
		const int kMacrosY = anIntVec[1];
		anIntVec[0] = 244; anIntVec[1] = 40;
		Profile::getIntArray("HUD/MacroWH", anIntVec);
		this->macroButtonW = max(1, anIntVec[0]);
		this->macroButtonH = max(1, anIntVec[1]);
		this->macroButtonPos[eCmdDir_U].x = kMacrosX+macroButtonW/2;
		this->macroButtonPos[eCmdDir_U].y = kMacrosY;
		this->macroButtonPos[eCmdDir_L].x = kMacrosX;
		this->macroButtonPos[eCmdDir_L].y = kMacrosY + macroButtonH;
		this->macroButtonPos[eCmdDir_R].x = kMacrosX + macroButtonW;
		this->macroButtonPos[eCmdDir_R].y = macroButtonPos[eCmdDir_L].y;
		this->macroButtonPos[eCmdDir_D].x = macroButtonPos[eCmdDir_U].x;
		this->macroButtonPos[eCmdDir_D].y = macroButtonPos[eCmdDir_L].y + macroButtonH;
		this->borderThickness = max(0, Profile::getInt("HUD/BorderSize", 1));
		anIntVec.resize(3);
		anIntVec[0] = anIntVec[1] = anIntVec[2] = 100;
		Profile::getIntArray("HUD/BorderRGB", anIntVec);
		this->borderColor = RGB(anIntVec[0], anIntVec[1], anIntVec[2]);
		anIntVec[0] = anIntVec[1] = anIntVec[2] = 150;
		Profile::getIntArray("HUD/ButtonRGB", anIntVec);
		this->buttonColor = RGB(anIntVec[0], anIntVec[1], anIntVec[2]);
		anIntVec[0] = anIntVec[1] = anIntVec[2] = 10;
		Profile::getIntArray("HUD/LabelRGB", anIntVec);
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
static std::wstring sErrorMessage;
static std::wstring sNoticeMessage;
static int sErrorMessageTimer = 0;
static int sNoticeMessageTimer = 0;
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
	if( sErrorMessageTimer > 0 )
	{
		sErrorMessageTimer -= gAppFrameTime;
		if( sErrorMessageTimer <= 0 )
		{
			sErrorMessageTimer = 0;
			sErrorMessage.clear();
			OverlayWindow::redraw();
		}
	}
	else if( !gErrorString.empty() && !hadFatalError() )
	{
		sErrorMessage =
			widen("MMOGO ERROR: ") +
			gErrorString +
			widen("\nCheck errorlog.txt for other possible errors!");
		gErrorString.clear();
		sErrorMessageTimer = max(kNoticeStringMinTime,
			int(kNoticeStringDisplayTimePerChar * gErrorString.size()));
		OverlayWindow::redraw();
	}
	
	if( sNoticeMessageTimer > 0 )
	{
		sNoticeMessageTimer -= gAppFrameTime;
		if( sNoticeMessageTimer <= 0 )
		{
			sNoticeMessageTimer = 0;
			sNoticeMessage.clear();
			OverlayWindow::redraw();
		}
	}
	if( !gNoticeString.empty() )
	{
		sNoticeMessageTimer = max(kNoticeStringMinTime,
			int(kNoticeStringDisplayTimePerChar * gNoticeString.size()));
		sNoticeMessage = gNoticeString;
		gNoticeString.clear();
		OverlayWindow::redraw();
	}

}


void render(HWND theWindow, RECT theClientRect)
{
	// Prepare to draw
	PAINTSTRUCT aPaintStruct;
	HDC hdc = BeginPaint(theWindow, &aPaintStruct);
	if( !sInitialized )
	{
		EndPaint(theWindow, &aPaintStruct);
		return;
	}

	HFONT anOldFont = (HFONT)GetCurrentObject(hdc, OBJ_FONT);
	HBRUSH hOldBrush = (HBRUSH)GetCurrentObject(hdc, OBJ_BRUSH);
	HPEN hOldPen = (HPEN)GetCurrentObject(hdc, OBJ_PEN);

	#ifdef DEBUG_DRAW_WINDOW_FRAME
	{
		// Normally I'd prefer not to create and destroy these on-the-fly,
		// but since this is purely for debugging purposes...
		HBRUSH hBlackBrush = CreateSolidBrush(RGB(0, 0, 0));
		HPEN hRedPen = CreatePen(PS_INSIDEFRAME, 3, RGB(255, 0, 0));
		SelectObject(hdc, hRedPen);
		SelectObject(hdc, hBlackBrush);
		Rectangle(hdc,
			theClientRect.left, theClientRect.top,
			theClientRect.right, theClientRect.bottom);
		SelectObject(hdc, hOldPen);
		SelectObject(hdc, hOldBrush);
		DeleteObject(hRedPen);
		DeleteObject(hBlackBrush);
	}
	#endif

	// Draw Macro Sets HUD element
	if( gVisibleHUD.test(eHUDElement_Macros) )
	{
		SelectObject(hdc, sFont);
		SetBkColor(hdc, kConfig.buttonColor);
		SetTextColor(hdc, kConfig.labelColor);

		// Draw each button
		for(int i = 0; i < 4; ++i)
		{
			RECT aButtonRect = theClientRect;
			aButtonRect.left += kConfig.macroButtonPos[i].x;
			aButtonRect.top += kConfig.macroButtonPos[i].y;
			aButtonRect.right = aButtonRect.left + kConfig.macroButtonW;
			aButtonRect.bottom = aButtonRect.top + kConfig.macroButtonH;
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

	}

	// Draw error string
	if( sErrorMessageTimer > 0 )
	{
		SelectObject(hdc, anOldFont);
		RECT aTextRect = theClientRect;
		InflateRect(&aTextRect, -8, -8);
		SetBkColor(hdc, RGB(255, 0, 0));
		SetTextColor(hdc, RGB(255, 255, 0));
		DrawText(hdc, sErrorMessage.c_str(), -1, &aTextRect,
			DT_WORDBREAK);
	}

	// Draw notice string
	if( sNoticeMessageTimer > 0 )
	{
		SelectObject(hdc, anOldFont);
		RECT aTextRect = theClientRect;
		InflateRect(&aTextRect, -8, -8);
		SetBkColor(hdc, RGB(0, 0, 255));
		SetTextColor(hdc, RGB(255, 255, 255));
		DrawText(hdc, sNoticeMessage.c_str(), -1, &aTextRect,
			DT_RIGHT | DT_BOTTOM | DT_SINGLELINE);
	}

	// Clean up
	SelectObject(hdc, anOldFont);
	SelectObject(hdc, hOldBrush);
	SelectObject(hdc, hOldPen);
	EndPaint(theWindow, &aPaintStruct);		
}

} // HUD
