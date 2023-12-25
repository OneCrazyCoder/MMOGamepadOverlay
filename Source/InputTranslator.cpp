//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputTranslator.h"

#include "Gamepad.h"
#include "InputDispatcher.h"
#include "InputMap.h"
#include "OverlayWindow.h" // to set alpha fade in/out
#include "Profile.h"

namespace InputTranslator
{

//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void update()
{
	using namespace Gamepad;

	s16 aMouseMoveX = 0;
	s16 aMouseMoveY = 0;
	bool aMouseMoveDigital = false;

	if( buttonDown(eBtn_L2) )
	{
		OverlayWindow::startAutoFadeOutTimer();
		OverlayWindow::fadeFullyIn();
		if( buttonHit(eBtn_L2) )
		{
			if( gMacroSetID != 0 )
			{
				gMacroSetID = 0;
				OverlayWindow::redraw();
			}
		}

		int aMacroSlotRequested = 0;
		if( buttonHit(eBtn_FUp) )
			aMacroSlotRequested = 1;
		else if( buttonHit(eBtn_FLeft) )
			aMacroSlotRequested = 2;
		else if( buttonHit(eBtn_FRight) )
			aMacroSlotRequested = 3;
		else if( buttonHit(eBtn_FDown) )
			aMacroSlotRequested = 4;
		else if( buttonHit(eBtn_DUp) )
			aMacroSlotRequested = 1;
		else if( buttonHit(eBtn_DLeft) )
			aMacroSlotRequested = 2;
		else if( buttonHit(eBtn_DRight) )
			aMacroSlotRequested = 3;
		else if( buttonHit(eBtn_DDown) )
			aMacroSlotRequested = 4;

		if( aMacroSlotRequested )
		{
			const std::string& aMacro =
				InputMap::getMacroOutput(
					gMacroSetID,
					aMacroSlotRequested-1);

			if( aMacro.empty() )
			{
				// Do nothing
			}
			else if( aMacro[0] == kMacroSetChangeChar )
			{// Change macro set
				switch(aMacro.size())
				{
				case 2:
					gMacroSetID = aMacro[1];
					break;
				case 3:
					gMacroSetID = (u16)aMacro[1] | (u16(aMacro[2]) << 8);
					break;
				default:
					DBG_ASSERT(false);
				}
				OverlayWindow::redraw();
			}
			else
			{// Dispatch macro
				InputDispatcher::sendMacro(aMacro);
			}
		}
	}
	else if( !buttonDown(eBtn_R2) )
	{
		// Can move mouse with d-pad while not selecting macro
		if( buttonDown(eBtn_DLeft) )
			aMouseMoveX -= 255;
		if( buttonDown(eBtn_DRight) )
			aMouseMoveX += 255;
		if( buttonDown(eBtn_DUp) )
			aMouseMoveY -= 255;
		if( buttonDown(eBtn_DDown) )
			aMouseMoveY += 255;
		if( aMouseMoveX || aMouseMoveY )
			aMouseMoveDigital = true;
	}

	if( buttonDown(eBtn_R2) )
	{
		// Scroll mouse wheel with right stick or d-pad
		s16 aMouseWheelY = 0;
		if( buttonDown(eBtn_DUp) )
			aMouseWheelY -= 255;
		if( buttonDown(eBtn_DDown) )
			aMouseWheelY += 255;
		if( aMouseWheelY )
			aMouseMoveDigital = true;
		if( u8 anAxisVal = buttonAnalogVal(eBtn_RSDown) )
			aMouseWheelY += anAxisVal;
		if( u8 anAxisVal = buttonAnalogVal(eBtn_RSUp) )
			aMouseWheelY -= anAxisVal;
		InputDispatcher::scrollMouseWheel(aMouseWheelY, aMouseMoveDigital);
	}
	else
	{
		// Move mouse cursor with right analog stick
		if( u8 anAxisVal = buttonAnalogVal(eBtn_RSRight) )
			aMouseMoveX += anAxisVal;
		if( u8 anAxisVal = buttonAnalogVal(eBtn_RSLeft) )
			aMouseMoveX -= anAxisVal;
		if( u8 anAxisVal = buttonAnalogVal(eBtn_RSDown) )
			aMouseMoveY += anAxisVal;
		if( u8 anAxisVal = buttonAnalogVal(eBtn_RSUp) )
			aMouseMoveY -= anAxisVal;
	}
	
	InputDispatcher::shiftMouseCursor(
		aMouseMoveX, aMouseMoveY,
		aMouseMoveDigital);
}

} // InputTranslator
