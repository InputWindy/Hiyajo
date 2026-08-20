#include "Log.h"

#include <spdlog/spdlog.h>

namespace Maho
{

namespace Log
{

bool FLog::ExecuteStage(ELogStage Stage)
{
	switch (Stage)
	{
	case ELogStage::Init:
		spdlog::set_level(spdlog::level::info);
		break;

	case ELogStage::Shutdown:
		// Flush buffered stdout before tearing the logger down — GUI apps
		// (WinMain + FreeConsole) lose buffered messages at exit otherwise.
		if (spdlog::default_logger() != nullptr)
		{
			spdlog::default_logger()->flush();
		}
		spdlog::shutdown();
		break;
	}
	return true;
}

void SetLogLevel(ELogLevel Level)
{
	switch (Level)
	{
	case ELogLevel::Debug:
		spdlog::set_level(spdlog::level::debug);
		break;
	case ELogLevel::Info:
		spdlog::set_level(spdlog::level::info);
		break;
	case ELogLevel::Warn:
		spdlog::set_level(spdlog::level::warn);
		break;
	case ELogLevel::Error:
		spdlog::set_level(spdlog::level::err);
		break;
	case ELogLevel::Off:
		spdlog::set_level(spdlog::level::off);
		break;
	}
}

void Debug(const char* Message)
{
	spdlog::debug(Message);
}

void Info(const char* Message)
{
	spdlog::info(Message);
}

void Warn(const char* Message)
{
	spdlog::warn(Message);
}

void Error(const char* Message)
{
	spdlog::error(Message);
}

} // namespace Log

} // namespace Maho
