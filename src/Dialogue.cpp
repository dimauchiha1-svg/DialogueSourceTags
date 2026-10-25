#include "Dialogue.h"

#include "Settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <set>
#include <string_view>
#include <unordered_map>

namespace Dialogue
{
	namespace
	{
		constexpr std::size_t kProcessMessageIndex = 0x4;  // IMenu::ProcessMessage

		using ProcessMessage_t = RE::UI_MESSAGE_RESULTS (*)(RE::IMenu*, RE::UIMessage&);
		ProcessMessage_t g_original = nullptr;

		int g_loggedLines = 0;
		constexpr int kMaxLoggedLines = 60;

		// Lines we have changed: dialogue entry -> original text and the text we produced.
		// Lets us re-tag a line when the settings change (the old tag is replaced, not duplicated).
		struct Tagged
		{
			std::string original;
			std::string tagged;
		};
		std::unordered_map<const void*, Tagged> g_tagged;  // used only from the UI thread
		constexpr size_t                        kMaxTagged = 2048;

		std::mutex            g_seenLock;
		std::set<std::string> g_seen;  // plugin file names (original case)

		std::mutex                                 g_tagLock;
		std::unordered_map<std::string, std::string> g_tagFiles;  // tag text -> plugin file name it stands for

		void NoteTag(const std::string& a_tag, const std::string& a_fileName)
		{
			std::scoped_lock lock(g_tagLock);
			g_tagFiles[a_tag] = a_fileName;
		}

		void NoteSeen(const std::string& a_fileName)
		{
			std::scoped_lock lock(g_seenLock);
			g_seen.insert(a_fileName);
		}

		bool IsOfficial(const std::string& a_lowerName)
		{
			return a_lowerName == "skyrim.esm" || a_lowerName == "update.esm" || a_lowerName == "dawnguard.esm" ||
			       a_lowerName == "hearthfires.esm" || a_lowerName == "dragonborn.esm";
		}

		// Stable color from the file name: hue from a hash, fixed saturation and brightness (readable on a dark background).
		std::string AutoColor(const std::string& a_lowerName)
		{
			std::uint32_t h = 2166136261u;
			for (const unsigned char c : a_lowerName) {
				h ^= c;
				h *= 16777619u;
			}
			const double hue = static_cast<double>(h % 360u);
			const double s = 0.55;
			const double v = 1.0;

			const double c = v * s;
			const double hp = hue / 60.0;
			const double x = c * (1.0 - std::abs(std::fmod(hp, 2.0) - 1.0));
			double       r = 0, g = 0, b = 0;
			if (hp < 1) {
				r = c, g = x;
			} else if (hp < 2) {
				r = x, g = c;
			} else if (hp < 3) {
				g = c, b = x;
			} else if (hp < 4) {
				g = x, b = c;
			} else if (hp < 5) {
				r = x, b = c;
			} else {
				r = c, b = x;
			}
			const double m = v - c;
			char         buf[8];
			std::snprintf(buf, sizeof(buf), "%02X%02X%02X",
				static_cast<int>((r + m) * 255.0 + 0.5), static_cast<int>((g + m) * 255.0 + 0.5), static_cast<int>((b + m) * 255.0 + 0.5));
			return buf;
		}

		// Display name of a plugin: alias from the INI, otherwise the file name (without extension by default), truncated.
		std::string DisplayName(const Settings::Data& cfg, const std::string& a_fileName)
		{
			const auto  lower = Settings::ToLower(a_fileName);

			if (const auto it = cfg.names.find(lower); it != cfg.names.end()) {
				return it->second;
			}

			std::string name = a_fileName;
			if (!cfg.showExtension) {
				const auto dot = name.find_last_of('.');
				if (dot != std::string::npos && dot > 0) {
					name.erase(dot);
				}
			}
			if (cfg.maxTagLength > 0 && name.size() > static_cast<size_t>(cfg.maxTagLength)) {
				name.resize(static_cast<size_t>(cfg.maxTagLength));
			}
			return name;
		}

		std::string ColorFor(const Settings::Data& cfg, const std::string& a_fileName)
		{
			const auto  lower = Settings::ToLower(a_fileName);

			if (const auto it = cfg.colors.find(lower); it != cfg.colors.end()) {
				return it->second;
			}
			if (IsOfficial(lower)) {
				return "A0A0A0";  // official files: muted grey
			}
			return AutoColor(lower);
		}

		// Builds the prefix for one dialogue line. Empty = leave the line untouched.
		std::string BuildPrefix(const Settings::Data& cfg, const RE::TESTopicInfo* a_info)
		{
			if (!a_info) {
				return {};
			}

			const RE::TESFile* creator = a_info->GetFile(0);
			const RE::TESFile* last = a_info->GetFile(-1);
			if (!creator && !last) {
				return {};
			}

			const RE::TESFile* primary = creator;
			if (cfg.source == Settings::SourceMode::kLastModifier) {
				primary = last;
			}
			if (!primary) {
				primary = creator ? creator : last;
			}

			const std::string primaryName(primary->GetFilename());
			if (primaryName.empty()) {
				return {};
			}
			NoteSeen(primaryName);
			if (cfg.hideOfficial && IsOfficial(Settings::ToLower(primaryName))) {
				return {};
			}

			std::string tag = DisplayName(cfg, primaryName);
			if (cfg.source == Settings::SourceMode::kBoth && creator && last && creator != last) {
				const std::string lastName(last->GetFilename());
				NoteSeen(lastName);
				tag += "/" + DisplayName(cfg, lastName);
			}

			// The tag is plain text. When colors are enabled, the Flash side colors it later (see Recolor.cpp),
			// looking the color up by the tag text.
			if (cfg.useColor) {
				NoteTag(tag, primaryName);
			}
			return "[" + tag + "] ";
		}

		// Prepends the tag to the text of every response option. Safe to call repeatedly.
		void TagDialogueList()
		{
			const auto cfgPtr = Settings::Get();
			const auto& cfg = *cfgPtr;
			if (!cfg.enabled && g_tagged.empty()) {
				return;
			}

			auto* mgr = RE::MenuTopicManager::GetSingleton();
			if (!mgr || !mgr->dialogueList) {
				return;
			}

			if (g_tagged.size() > kMaxTagged) {
				g_tagged.clear();
			}

			for (auto* dlg : *mgr->dialogueList) {
				if (!dlg || !dlg->parentTopicInfo) {
					continue;
				}
				const char* raw = dlg->topicText.c_str();
				if (!raw) {
					continue;
				}

				const std::string current(raw);
				const auto        prefix = cfg.enabled ? BuildPrefix(cfg, dlg->parentTopicInfo) : std::string();

				// Work out the untouched original text of the line.
				std::string original = current;
				const auto  it = g_tagged.find(dlg);
				if (it != g_tagged.end()) {
					if (it->second.tagged == current) {
						original = it->second.original;
					} else {
						g_tagged.erase(it);  // the entry was reused for another line
					}
				} else if (!prefix.empty() && current.starts_with(prefix)) {
					original = current.substr(prefix.size());
				}

				const std::string desired = prefix.empty() ? original : prefix + original;
				if (desired != current) {
					dlg->topicText = desired;
					if (cfg.log && g_loggedLines < kMaxLoggedLines && !prefix.empty()) {
						++g_loggedLines;
						spdlog::info("Tagged line (INFO {:08X}): {}", dlg->parentTopicInfo->GetFormID(), desired);
					}
				}

				if (prefix.empty()) {
					g_tagged.erase(dlg);
				} else {
					g_tagged[dlg] = Tagged{ original, desired };
				}
			}
		}

		RE::UI_MESSAGE_RESULTS Hook_ProcessMessage(RE::IMenu* a_menu, RE::UIMessage& a_message)
		{
			const auto type = a_message.type.get();
			if (type == RE::UI_MESSAGE_TYPE::kUpdate || type == RE::UI_MESSAGE_TYPE::kShow || type == RE::UI_MESSAGE_TYPE::kReshow) {
				TagDialogueList();
			}
			return g_original(a_menu, a_message);
		}
	}

	std::string ResolveColor(const Settings::Data& a_cfg, const std::string& a_fileName)
	{
		return ColorFor(a_cfg, a_fileName);
	}

	std::string ResolveName(const Settings::Data& a_cfg, const std::string& a_fileName)
	{
		return DisplayName(a_cfg, a_fileName);
	}

	bool LookupTagColor(const std::string& a_tag, std::uint32_t& a_rgb)
	{
		std::string file;
		{
			std::scoped_lock lock(g_tagLock);
			const auto       it = g_tagFiles.find(a_tag);
			if (it == g_tagFiles.end()) {
				return false;
			}
			file = it->second;
		}

		const auto  cfg = Settings::Get();
		const auto  hex = ColorFor(*cfg, file);
		unsigned    v = 0;
		if (std::sscanf(hex.c_str(), "%x", &v) != 1) {
			return false;
		}
		a_rgb = v & 0xFFFFFF;
		return true;
	}

	std::vector<std::string> GetSeenPlugins()
	{
		std::scoped_lock lock(g_seenLock);
		return { g_seen.begin(), g_seen.end() };
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_DialogueMenu[0] };
		g_original = reinterpret_cast<ProcessMessage_t>(vtbl.write_vfunc(kProcessMessageIndex, reinterpret_cast<std::uintptr_t>(&Hook_ProcessMessage)));
		spdlog::info("Hook installed on DialogueMenu::ProcessMessage");
	}
}
