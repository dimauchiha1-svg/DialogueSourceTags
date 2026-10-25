#include "Settings.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace Settings
{
	namespace
	{
		constexpr auto kIniPath = "Data/SKSE/Plugins/DialogueSourceTags.ini";

		std::mutex                  g_lock;
		std::shared_ptr<const Data> g_data = std::make_shared<const Data>();

		std::string Trim(std::string_view a_text)
		{
			size_t b = 0;
			size_t e = a_text.size();
			while (b < e && std::isspace(static_cast<unsigned char>(a_text[b]))) {
				++b;
			}
			while (e > b && std::isspace(static_cast<unsigned char>(a_text[e - 1]))) {
				--e;
			}
			return std::string(a_text.substr(b, e - b));
		}

		bool ParseBool(const std::string& a_value, bool a_default)
		{
			const auto v = ToLower(a_value);
			if (v == "1" || v == "true" || v == "yes" || v == "on") {
				return true;
			}
			if (v == "0" || v == "false" || v == "no" || v == "off") {
				return false;
			}
			return a_default;
		}

		bool IsHexColor(const std::string& a_text)
		{
			return a_text.size() == 6 && std::all_of(a_text.begin(), a_text.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
		}
	}

	std::string ToLower(std::string_view a_text)
	{
		std::string out(a_text);
		std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	std::shared_ptr<const Data> Get()
	{
		std::scoped_lock lock(g_lock);
		return g_data;
	}

	void Set(Data a_data)
	{
		auto next = std::make_shared<const Data>(std::move(a_data));
		std::scoped_lock lock(g_lock);
		g_data = std::move(next);
	}

	void Load()
	{
		Data d;

		std::ifstream file(kIniPath);
		if (!file) {
			spdlog::info("INI not found ({}), using defaults", kIniPath);
			Set(std::move(d));
			return;
		}

		std::string section;
		std::string line;
		while (std::getline(file, line)) {
			// Strip a UTF-8 BOM on the first line.
			if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
				line.erase(0, 3);
			}
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#') {
				continue;
			}
			if (line.front() == '[' && line.back() == ']') {
				section = ToLower(Trim(std::string_view(line).substr(1, line.size() - 2)));
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			const auto key = Trim(std::string_view(line).substr(0, eq));
			auto       value = Trim(std::string_view(line).substr(eq + 1));
			// Trailing comment ("; ...") after the value.
			if (const auto sc = value.find(" ;"); sc != std::string::npos) {
				value = Trim(std::string_view(value).substr(0, sc));
			}
			const auto lkey = ToLower(key);

			if (section == "general") {
				if (lkey == "enabled") {
					d.enabled = ParseBool(value, d.enabled);
				} else if (lkey == "source") {
					const auto v = ToLower(value);
					if (v == "creator") {
						d.source = SourceMode::kCreator;
					} else if (v == "lastmodifier") {
						d.source = SourceMode::kLastModifier;
					} else if (v == "both") {
						d.source = SourceMode::kBoth;
					}
				} else if (lkey == "usecolor") {
					d.useColor = ParseBool(value, d.useColor);
				} else if (lkey == "hideofficial") {
					d.hideOfficial = ParseBool(value, d.hideOfficial);
				} else if (lkey == "showextension") {
					d.showExtension = ParseBool(value, d.showExtension);
				} else if (lkey == "maxtaglength") {
					try {
						d.maxTagLength = std::clamp(std::stoi(value), 0, 64);
					} catch (...) {
					}
				} else if (lkey == "log") {
					d.log = ParseBool(value, d.log);
				}
			} else if (section == "colors") {
				auto color = value;
				if (!color.empty() && color[0] == '#') {
					color.erase(0, 1);
				}
				if (IsHexColor(color)) {
					d.colors[lkey] = color;
				} else {
					spdlog::warn("[Colors] '{}': '{}' is not a RRGGBB color, ignored", key, value);
				}
			} else if (section == "names") {
				if (!value.empty()) {
					d.names[lkey] = value;
				}
			}
		}

		spdlog::info("Settings: enabled={} source={} color={} hideOfficial={} showExtension={} maxTagLength={} colorOverrides={} nameOverrides={}",
			d.enabled, static_cast<int>(d.source), d.useColor, d.hideOfficial, d.showExtension, d.maxTagLength, d.colors.size(), d.names.size());
		Set(std::move(d));
	}

	namespace
	{
		const char* SourceToString(SourceMode a_mode)
		{
			switch (a_mode) {
			case SourceMode::kLastModifier:
				return "LastModifier";
			case SourceMode::kBoth:
				return "Both";
			default:
				return "Creator";
			}
		}

		// Inserts a block of lines before the trailing blank lines of a section.
		void AppendToSection(std::vector<std::string>& a_lines, const std::vector<std::string>& a_add)
		{
			if (a_add.empty()) {
				return;
			}
			size_t blanks = 0;
			while (!a_lines.empty() && Trim(a_lines.back()).empty()) {
				a_lines.pop_back();
				++blanks;
			}
			a_lines.insert(a_lines.end(), a_add.begin(), a_add.end());
			for (size_t i = 0; i < blanks; ++i) {
				a_lines.emplace_back();
			}
		}
	}

	bool Save()
	{
		const auto cfg = Get();

		const std::vector<std::pair<std::string, std::string>> general = {
			{ "Enabled", cfg->enabled ? "1" : "0" },
			{ "Source", SourceToString(cfg->source) },
			{ "UseColor", cfg->useColor ? "1" : "0" },
			{ "HideOfficial", cfg->hideOfficial ? "1" : "0" },
			{ "ShowExtension", cfg->showExtension ? "1" : "0" },
			{ "MaxTagLength", std::to_string(cfg->maxTagLength) },
			{ "Log", cfg->log ? "1" : "0" },
		};
		const std::map<std::string, std::string> colors(cfg->colors.begin(), cfg->colors.end());
		const std::map<std::string, std::string> names(cfg->names.begin(), cfg->names.end());

		std::vector<std::string> in;
		{
			std::ifstream file(kIniPath);
			std::string   line;
			while (std::getline(file, line)) {
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				in.push_back(line);
			}
		}

		std::vector<std::string> out;
		std::string              section;
		std::set<std::string>    generalDone;
		std::set<std::string>    sectionsSeen;

		const auto flush = [&](const std::string& a_section) {
			std::vector<std::string> add;
			if (a_section == "general") {
				for (const auto& [k, v] : general) {
					if (!generalDone.contains(ToLower(k))) {
						add.push_back(k + "=" + v);
					}
				}
			} else if (a_section == "colors") {
				for (const auto& [k, v] : colors) {
					add.push_back(k + "=" + v);
				}
			} else if (a_section == "names") {
				for (const auto& [k, v] : names) {
					add.push_back(k + "=" + v);
				}
			}
			AppendToSection(out, add);
		};

		for (const auto& raw : in) {
			const auto line = Trim(raw);
			if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
				flush(section);
				section = ToLower(Trim(std::string_view(line).substr(1, line.size() - 2)));
				sectionsSeen.insert(section);
				out.push_back(raw);
				continue;
			}
			if (line.empty() || line[0] == ';' || line[0] == '#') {
				out.push_back(raw);
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				out.push_back(raw);
				continue;
			}
			const auto lkey = ToLower(Trim(std::string_view(line).substr(0, eq)));
			if (section == "general") {
				bool replaced = false;
				for (const auto& [k, v] : general) {
					if (ToLower(k) == lkey) {
						out.push_back(k + "=" + v);
						generalDone.insert(lkey);
						replaced = true;
						break;
					}
				}
				if (!replaced) {
					out.push_back(raw);
				}
			} else if (section == "colors" || section == "names") {
				// Rewritten from the current settings at the end of the section.
			} else {
				out.push_back(raw);
			}
		}
		flush(section);

		for (const char* sec : { "General", "Colors", "Names" }) {
			if (!sectionsSeen.contains(ToLower(sec))) {
				if (!out.empty() && !Trim(out.back()).empty()) {
					out.emplace_back();
				}
				out.push_back(std::string("[") + sec + "]");
				const auto lsec = ToLower(sec);
				if (lsec == "general") {
					for (const auto& [k, v] : general) {
						out.push_back(k + "=" + v);
					}
				} else if (lsec == "colors") {
					for (const auto& [k, v] : colors) {
						out.push_back(k + "=" + v);
					}
				} else {
					for (const auto& [k, v] : names) {
						out.push_back(k + "=" + v);
					}
				}
			}
		}

		std::ofstream file(kIniPath, std::ios::trunc);
		if (!file) {
			spdlog::error("Could not write {}", kIniPath);
			return false;
		}
		for (const auto& l : out) {
			file << l << "\n";
		}
		spdlog::info("Settings saved to {}", kIniPath);
		return true;
	}
}
