#pragma once

// Colors the "[PluginName]" tags in the dialogue list.
//
// The dialogue list sets the text color of the whole line itself, so a color inside the text is lost.
// Instead, every frame the dialogue menu is open, the tag part of each entry gets its own text format
// (Flash TextField.setTextFormat), which survives the list redrawing its entries.
namespace Recolor
{
	// Installs the hook on DialogueMenu::AdvanceMovie. Call after kDataLoaded.
	void Install();
}
