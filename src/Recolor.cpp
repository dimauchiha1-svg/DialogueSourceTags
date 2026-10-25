#include "Recolor.h"

#include "Dialogue.h"
#include "Settings.h"

#include <string>

namespace Recolor
{
	namespace
	{
		constexpr std::size_t kAdvanceMovieIndex = 0x5;  // IMenu::AdvanceMovie

		using AdvanceMovie_t = void (*)(RE::IMenu*, float, std::uint32_t);
		AdvanceMovie_t g_original = nullptr;

		// Candidate paths of the topic list (the first one that exists is used).
		constexpr const char* kListPaths[] = {
			"_root.DialogueMenu_mc.TopicListHolder.List",
			"_root.DialogueMenu_mc.TopicList",
			"_root.DialogueMenu_mc.TopicListHolder.TopicList",
			"_root.DialogueMenu_mc.TopicListHolder",
		};

		// Scaleform types of clips, text fields and objects are >= 6 (see Scaleform ValueType).
		bool IsClip(const RE::GFxValue& a_value)
		{
			const auto t = static_cast<int>(a_value.GetType());
			return t >= 6 && t <= 8;
		}

		bool GetMemberSafe(RE::GFxValue& a_obj, const char* a_name, RE::GFxValue& a_out)
		{
			RE::GFxValue v;
			if (!a_obj.GetMember(a_name, &v) || v.IsUndefined()) {
				return false;
			}
			a_out = v;
			return true;
		}

		// Number of UTF-16 code units in the first a_bytes bytes of a UTF-8 string (BMP only is enough here).
		std::size_t Utf16Length(const std::string& a_utf8, std::size_t a_bytes)
		{
			std::size_t n = 0;
			for (std::size_t i = 0; i < a_bytes && i < a_utf8.size(); ++i) {
				const auto c = static_cast<unsigned char>(a_utf8[i]);
				if ((c & 0xC0) != 0x80) {
					++n;
					if (c >= 0xF0) {
						++n;  // outside the BMP: a surrogate pair
					}
				}
			}
			return n;
		}

		// Colors the tag at the start of one entry's text field. Returns true if a tag was found.
		bool ColorEntry(RE::GFxMovieView* a_movie, RE::GFxValue& a_textField)
		{
			RE::GFxValue text;
			if (!GetMemberSafe(a_textField, "text", text) || !text.IsString()) {
				return false;
			}
			const std::string s = text.GetString();
			if (s.size() < 3 || s[0] != '[') {
				return false;
			}
			const auto close = s.find(']');
			if (close == std::string::npos) {
				return false;
			}

			std::uint32_t rgb = 0;
			if (!Dialogue::LookupTagColor(s.substr(1, close - 1), rgb)) {
				return false;
			}

			RE::GFxValue fmt;
			a_movie->CreateObject(&fmt, "TextFormat");
			fmt.SetMember("color", RE::GFxValue(static_cast<double>(rgb)));

			const RE::GFxValue args[3] = { RE::GFxValue(0.0), RE::GFxValue(static_cast<double>(Utf16Length(s, close + 1))), fmt };
			a_textField.Invoke("setTextFormat", nullptr, args, 3);
			return true;
		}

		struct State
		{
			bool        pathLogged = false;
			bool        diagLogged = false;
			int         emptyFrames = 0;
			const char* path = nullptr;
		} g_state;

		// The list keeps its entry clips as members named Entry0, Entry1, ... (BSScrollingList).
		constexpr int kMaxEntries = 32;

		int ColorList(RE::GFxMovieView* a_movie, RE::GFxValue& a_list)
		{
			int colored = 0;
			for (int i = 0; i < kMaxEntries; ++i) {
				const std::string name = "Entry" + std::to_string(i);

				RE::GFxValue clip;
				if (!GetMemberSafe(a_list, name.c_str(), clip)) {
					break;
				}
				RE::GFxValue tf;
				if (GetMemberSafe(clip, "textField", tf) && ColorEntry(a_movie, tf)) {
					++colored;
				}
			}
			return colored;
		}

		// Diagnostics, written once: which candidate paths exist and what the first entry looks like.
		void LogStructure(RE::GFxMovieView* a_movie)
		{
			spdlog::info("Recolor: no colored tag found, logging the dialogue list structure once");
			for (const auto* path : kListPaths) {
				RE::GFxValue list;
				if (!a_movie->GetVariable(&list, path) || !IsClip(list)) {
					spdlog::info("  {}: not found", path);
					continue;
				}
				spdlog::info("  {}: found", path);

				RE::GFxValue entry, tf, text;
				if (!GetMemberSafe(list, "Entry0", entry)) {
					spdlog::info("    no member Entry0");
					continue;
				}
				if (!GetMemberSafe(entry, "textField", tf)) {
					spdlog::info("    Entry0 has no member textField");
					continue;
				}
				if (GetMemberSafe(tf, "text", text) && text.IsString()) {
					spdlog::info("    Entry0.textField.text = '{}'", text.GetString());
				} else {
					spdlog::info("    Entry0.textField has no string member text");
				}
			}
		}

		void ApplyColors(RE::IMenu* a_menu)
		{
			const auto cfg = Settings::Get();
			if (!cfg->enabled || !cfg->useColor) {
				return;
			}

			auto* movie = a_menu->uiMovie.get();
			if (!movie) {
				return;
			}

			int colored = 0;
			for (const auto* path : kListPaths) {
				RE::GFxValue list;
				if (!movie->GetVariable(&list, path) || !IsClip(list)) {
					continue;
				}
				colored = ColorList(movie, list);
				if (colored > 0) {
					if (!g_state.pathLogged) {
						g_state.pathLogged = true;
						spdlog::info("Recolor: tags colored through '{}' ({} entries)", path, colored);
					}
					g_state.emptyFrames = 0;
					return;
				}
			}

			// Nothing colored: normal when the list has no tagged entries yet. Dump the structure once if it persists.
			if (!g_state.diagLogged && ++g_state.emptyFrames == 600) {
				g_state.diagLogged = true;
				LogStructure(movie);
			}
		}

		void Hook_AdvanceMovie(RE::IMenu* a_menu, float a_interval, std::uint32_t a_currentTime)
		{
			g_original(a_menu, a_interval, a_currentTime);
			ApplyColors(a_menu);
		}
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_DialogueMenu[0] };
		g_original = reinterpret_cast<AdvanceMovie_t>(vtbl.write_vfunc(kAdvanceMovieIndex, reinterpret_cast<std::uintptr_t>(&Hook_AdvanceMovie)));
		spdlog::info("Hook installed on DialogueMenu::AdvanceMovie");
	}
}
