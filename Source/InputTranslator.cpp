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
}

} // InputTranslator
