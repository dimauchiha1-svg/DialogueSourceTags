#pragma once

#include "Settings.h"

#include <string>
#include <vector>

// Adds a "[PluginName]" tag in front of every player response option in the dialogue menu.
namespace Dialogue
{
	// Installs the hook on DialogueMenu::ProcessMessage. Call after kDataLoaded.
	void Install();

	// The color (RRGGBB) a plugin file gets with the given settings: INI override, grey for official files, otherwise automatic.
	std::string ResolveColor(const Settings::Data& a_cfg, const std::string& a_fileName);

	// The tag text a plugin file gets with the given settings (alias, extension and length rules applied).
	std::string ResolveName(const Settings::Data& a_cfg, const std::string& a_fileName);

	// Plugin files whose dialogue lines have been shown during this session (sorted, original case).
	std::vector<std::string> GetSeenPlugins();
}
