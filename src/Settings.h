#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

// Plugin settings, read from Data/SKSE/Plugins/DialogueSourceTags.ini.
namespace Settings
{
	enum class SourceMode
	{
		kCreator,       // the plugin that created the dialogue line (first file in the form's file list)
		kLastModifier,  // the last plugin that changed the line (the one whose version is in effect)
		kBoth           // both, if they differ
	};

	struct Data
	{
		bool        enabled = true;
		SourceMode  source = SourceMode::kCreator;
		bool        useColor = true;       // wrap the tag in <font color>
		bool        hideOfficial = false;  // do not tag lines from the official game files
		bool        showExtension = false; // "MyMod.esp" instead of "MyMod"
		int         maxTagLength = 16;     // 0 = no limit
		bool        log = true;            // write the first tagged lines to the log (diagnostics)

		// Lower-case plugin file name -> value
		std::unordered_map<std::string, std::string> colors;  // "RRGGBB"
		std::unordered_map<std::string, std::string> names;   // custom short names
	};

	// A snapshot of the current settings. Safe to use from any thread; a snapshot never changes.
	std::shared_ptr<const Data> Get();

	// Replaces the current settings (used by the in-game menu).
	void Set(Data a_data);

	// Reads the INI. If the file does not exist, default values stay in effect.
	void Load();

	// Writes the current settings to the INI. Comments and other lines of the file are preserved.
	// Returns false on a write error.
	bool Save();

	std::string ToLower(std::string_view a_text);
}
