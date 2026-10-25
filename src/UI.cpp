#include "UI.h"

#include <spdlog/spdlog.h>

#ifdef DIALOGUESOURCETAGS_MENU

#include "Dialogue.h"
#include "Settings.h"

#include <Windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>

// SKSE Menu Framework 3 API header. ImGui is called through the framework's own functions
// (ImGuiMCP namespace), so imgui.dll and ImGui libraries are not needed.
#include "SKSEMenuFramework.h"

namespace
{
	constexpr auto kFrameworkModule = L"SKSEMenuFramework";

	bool IsFrameworkInstalled()
	{
		return ::GetModuleHandleW(kFrameworkModule) != nullptr;
	}

	// The header calls framework functions via GetProcAddress without checking the result, so
	// before registering the page we make sure the needed functions exist in the installed framework version.
	bool HasRequiredExports()
	{
		static constexpr const char* kNames[] = {
			"AddSectionItem", "igGetVersion", "igGetCurrentContext", "igCheckbox", "igSliderInt", "igCombo_Str_arr",
			"igColorEdit3", "igInputText", "igTextColoredV", "igButton", "igSameLine", "igSeparator",
			"igPushID_Str", "igPopID", "igTextV", "igTextWrappedV", "igTextUnformatted", "igCollapsingHeader_TreeNodeFlags"
		};

		const auto module = ::GetModuleHandleW(kFrameworkModule);
		if (!module) {
			return false;
		}

		bool ok = true;
		for (const auto* name : kNames) {
			if (!::GetProcAddress(module, name)) {
				spdlog::error("Menu: SKSEMenuFramework.dll has no function {}", name);
				ok = false;
			}
		}
		return ok;
	}

	// Message about the result of the last action (saving / loading the INI).
	const char* g_status = "";

	bool ParseHex(const std::string& a_hex, float (&a_rgb)[3])
	{
		if (a_hex.size() != 6) {
			return false;
		}
		unsigned v = 0;
		if (std::sscanf(a_hex.c_str(), "%x", &v) != 1) {
			return false;
		}
		a_rgb[0] = static_cast<float>((v >> 16) & 0xFF) / 255.0f;
		a_rgb[1] = static_cast<float>((v >> 8) & 0xFF) / 255.0f;
		a_rgb[2] = static_cast<float>(v & 0xFF) / 255.0f;
		return true;
	}

	std::string ToHex(const float (&a_rgb)[3])
	{
		const auto c = [](float f) {
			const int i = static_cast<int>(f * 255.0f + 0.5f);
			return i < 0 ? 0 : (i > 255 ? 255 : i);
		};
		char buf[8];
		std::snprintf(buf, sizeof(buf), "%02X%02X%02X", c(a_rgb[0]), c(a_rgb[1]), c(a_rgb[2]));
		return buf;
	}

	void RenderPlugins(Settings::Data& a_cfg, bool& a_changed)
	{
		const auto seen = Dialogue::GetSeenPlugins();

		if (seen.empty()) {
			ImGuiMCP::TextWrapped("No plugins yet. Plugins appear here after their dialogue lines have been shown in the game. "
								  "You can also add colors and names for any plugin in the INI.");
			return;
		}

		for (const auto& file : seen) {
			const auto lower = Settings::ToLower(file);

			ImGuiMCP::PushID(file.c_str());

			float rgb[3] = { 1.0f, 1.0f, 1.0f };
			ParseHex(Dialogue::ResolveColor(a_cfg, file), rgb);
			ImGuiMCP::TextColored(ImGuiMCP::ImVec4{ rgb[0], rgb[1], rgb[2], 1.0f }, "[%s]", Dialogue::ResolveName(a_cfg, file).c_str());
			ImGuiMCP::SameLine();
			ImGuiMCP::Text("%s", file.c_str());

			if (ImGuiMCP::ColorEdit3("Color", rgb)) {
				a_cfg.colors[lower] = ToHex(rgb);
				a_changed = true;
			}

			char buf[64] = {};
			if (const auto it = a_cfg.names.find(lower); it != a_cfg.names.end()) {
				std::snprintf(buf, sizeof(buf), "%s", it->second.c_str());
			}
			if (ImGuiMCP::InputText("Short name (empty = default)", buf, sizeof(buf))) {
				if (buf[0] == '\0') {
					a_cfg.names.erase(lower);
				} else {
					a_cfg.names[lower] = buf;
				}
				a_changed = true;
			}

			if (ImGuiMCP::Button("Reset color and name")) {
				a_cfg.colors.erase(lower);
				a_cfg.names.erase(lower);
				a_changed = true;
			}

			ImGuiMCP::Separator();
			ImGuiMCP::PopID();
		}
	}

	void RenderBody()
	{
		// Diagnostics: the first call is written to the log BEFORE touching ImGui.
		static bool s_logged = false;
		if (!s_logged) {
			s_logged = true;
			spdlog::info("Menu: first Render call");
			spdlog::info("Menu: ImGui {}, context {}", ImGuiMCP::GetVersion(), static_cast<const void*>(ImGuiMCP::GetCurrentContext()));
		}

		// Work on a copy of the settings and publish it when something changed.
		Settings::Data cfg = *Settings::Get();
		bool           changed = false;

		ImGuiMCP::TextWrapped("Changes are applied the next time the dialogue list refreshes (re-open the dialogue or pick a topic).");
		ImGuiMCP::Separator();

		changed |= ImGuiMCP::Checkbox("Enabled", &cfg.enabled);

		int source = static_cast<int>(cfg.source);
		static const char* const kSources[] = { "Creator (plugin that made the line)", "Last modifier (version in effect)", "Both" };
		if (ImGuiMCP::Combo("Source", &source, kSources, 3)) {
			cfg.source = static_cast<Settings::SourceMode>(source);
			changed = true;
		}

		changed |= ImGuiMCP::Checkbox("Colored tags", &cfg.useColor);
		changed |= ImGuiMCP::Checkbox("Hide tags on official game files", &cfg.hideOfficial);
		changed |= ImGuiMCP::Checkbox("Show file extension (.esp / .esm / .esl)", &cfg.showExtension);
		changed |= ImGuiMCP::SliderInt("Max tag length (0 = no limit)", &cfg.maxTagLength, 0, 32);
		changed |= ImGuiMCP::Checkbox("Write tagged lines to the log", &cfg.log);

		ImGuiMCP::Separator();
		if (ImGuiMCP::CollapsingHeader("Plugins seen in dialogues", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
			RenderPlugins(cfg, changed);
		}

		if (changed) {
			Settings::Set(cfg);
		}

		ImGuiMCP::Separator();
		if (ImGuiMCP::Button("Save to INI")) {
			g_status = Settings::Save() ? "Saved to DialogueSourceTags.ini" : "Could not write DialogueSourceTags.ini (see log)";
		}
		ImGuiMCP::SameLine();
		if (ImGuiMCP::Button("Reload from INI")) {
			Settings::Load();
			g_status = "Reloaded from DialogueSourceTags.ini";
		}
		if (g_status[0] != '\0') {
			ImGuiMCP::TextUnformatted(g_status);
		}
	}

	// If an ImGui call still crashes, Windows raises an SEH exception. We catch it so as not to
	// take the game down: the page is disabled until restart.
	bool g_menuBroken = false;

	void LogMenuBroken()
	{
		spdlog::error("Menu: ImGui call crashed. The menu page is disabled until the game restarts, "
					  "settings are available in the INI.");
	}

	void __stdcall Render()
	{
		if (g_menuBroken) {
			return;
		}

		// This function has no objects with destructors, so __try can be used here.
		__try {
			RenderBody();
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			g_menuBroken = true;
		}

		if (g_menuBroken) {
			static bool s_logged = false;
			if (!s_logged) {
				s_logged = true;
				LogMenuBroken();
			}
		}
	}
}

namespace UI
{
	void Register()
	{
		if (!IsFrameworkInstalled()) {
			spdlog::info("SKSE Menu Framework is not installed, settings menu unavailable");
			return;
		}

		if (!HasRequiredExports()) {
			spdlog::error("Menu: the installed SKSE Menu Framework version is incompatible, settings menu disabled");
			return;
		}

		SKSEMenuFramework::SetSection("DialogueSourceTags");
		SKSEMenuFramework::AddSectionItem("Settings", Render);
		spdlog::info("DialogueSourceTags menu registered in SKSE Menu Framework");
	}
}

#else

namespace UI
{
	void Register()
	{
		spdlog::info("Plugin was built without SKSE Menu Framework support");
	}
}

#endif
