#include "Recolor.h"

#include "Dialogue.h"
#include "Settings.h"

#include <functional>
#include <utility>
#include <vector>
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

		// Calls a_fn(name, value) for every member of an object. Supports both visitor styles of CommonLibSSE.
		template <class F>
		bool VisitChildren(RE::GFxValue& a_obj, F&& a_fn)
		{
			using V = RE::GFxValue;
			if constexpr (requires(V& v, std::function<void(const char*, const V&)> fn) { v.VisitMembers(std::move(fn)); }) {
				a_obj.VisitMembers(std::function<void(const char*, const V&)>(std::forward<F>(a_fn)));
				return true;
			} else if constexpr (requires(V& v, typename V::ObjectVisitor* p) { v.VisitMembers(p); }) {
				struct Visitor : V::ObjectVisitor
				{
					explicit Visitor(F& a_f) :
						fn(a_f) {}
					void Visit(const char* a_name, const V& a_val) override { fn(a_name, a_val); }
					F& fn;
				};
				Visitor visitor(a_fn);
				a_obj.VisitMembers(&visitor);
				return true;
			} else {
				return false;
			}
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

		// Looks at the children of a list clip; entries are the ones that have a text field.
		int ColorList(RE::GFxMovieView* a_movie, RE::GFxValue& a_list, int a_depth)
		{
			int colored = 0;

			std::vector<std::pair<std::string, RE::GFxValue>> children;
			const bool                                        ok = VisitChildren(a_list, [&](const char* a_name, const RE::GFxValue& a_val) {
                if (a_name && IsClip(a_val)) {
                    children.emplace_back(a_name, a_val);
                }
            });
			if (!ok) {
				return 0;
			}

			for (auto& [name, child] : children) {
				RE::GFxValue tf;
				if (GetMemberSafe(child, "textField", tf)) {
					if (ColorEntry(a_movie, tf)) {
						++colored;
					}
				} else if (a_depth > 0 && name != "parent" && name != "_parent") {
					colored += ColorList(a_movie, child, a_depth - 1);
				}
			}
			return colored;
		}

		void LogStructure(RE::GFxMovieView* a_movie)
		{
			spdlog::info("Recolor: no colored tag found, dumping the dialogue menu structure once");
			const char* paths[] = { "_root.DialogueMenu_mc", "_root.DialogueMenu_mc.TopicListHolder", "_root.DialogueMenu_mc.TopicListHolder.List" };
			for (const auto* path : paths) {
				RE::GFxValue obj;
				if (!a_movie->GetVariable(&obj, path) || !IsClip(obj)) {
					spdlog::info("  {} not found", path);
					continue;
				}
				spdlog::info("  {}:", path);
				const bool ok = VisitChildren(obj, [&](const char* a_name, const RE::GFxValue& a_val) {
					spdlog::info("    member: {} (type {})", a_name ? a_name : "?", static_cast<int>(a_val.GetType()));
				});
				if (!ok) {
					spdlog::info("    member enumeration is not available in this CommonLibSSE version");
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
				colored = ColorList(movie, list, 2);
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
