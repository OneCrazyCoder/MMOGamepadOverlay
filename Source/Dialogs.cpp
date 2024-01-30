//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Dialogs.h"

namespace Dialogs
{

//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

ProfileSelectResult profileSelect(
	const std::vector<std::string>& theLoadableProfiles,
	const std::vector<std::string>& theTemplateProfiles)
{
	ProfileSelectResult result = { 0 };
	result.selectedIndex = 0;
	result.autoLoadRequested = false;
	result.cancelled = false;

	// TEMP - create a profile called "Profile" using default template #0
	result.newName = "Profile";

	// TODO: The actual dialog...

	return result;
}

} // Dialogs
