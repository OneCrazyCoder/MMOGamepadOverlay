//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Reads configuration files written by target application and updates
	overlay app to match automatically - such as modifying gUIScale to
	match the in-game setting for it.
*/


#include "Common.h"

namespace TargetConfigSync
{

// Load data from target app's config files and begin monitoring for changes
void load();

// Force reload data from target app config files even if they are unchanged
void refresh();

// Stop monitoring for file changes and free memory
void stop();

// Check for config file changes and update data as needed
void update();

} // TargetConfigSync
