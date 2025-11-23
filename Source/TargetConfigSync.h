//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

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
void loadProfileChanges();

// Stop monitoring for file changes and free memory
void cleanup();

// Check for config file changes and update data as needed
void update();

// Temporarily halt monitoring for file changes
void pauseMonitoring();
void resumeMonitoring();

// If have multiple possible target config sync files due to wildcards,
// this will force a dialog to pop up to select which one to use
void promptUserForSyncFileToUse();

} // TargetConfigSync
