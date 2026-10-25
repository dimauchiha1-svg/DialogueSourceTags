#include "Dialogue.h"
#include "Recolor.h"
#include "Settings.h"
#include "UI.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace
{
	void SetupLog()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			return;
		}
		*path /= "DialogueSourceTags.log";

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
	}

	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			// All SKSE plugins are loaded by now, so SKSE Menu Framework (if installed) is available.
			UI::Register();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			Dialogue::Install();
			Recolor::Install();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SetupLog();

	spdlog::info("DialogueSourceTags is loading");

	Settings::Load();

	SKSE::GetMessagingInterface()->RegisterListener(OnSKSEMessage);
	return true;
}
