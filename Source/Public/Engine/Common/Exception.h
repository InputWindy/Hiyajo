#pragma once

// Exception — non-fatal exception broadcast (engine Common, TSingleton).
// ReportException fans out to every OnException subscriber (logging, telemetry,
// crash reporters). Nothing aborts here; fatal errors go through Core/Fatal.
//
// Carries a minimal multicast event (TMulticastEvent) — the engine has no
// standalone delegate building block yet, and OnException only needs a
// void(const std::string&) broadcast.
#include <Core/Singleton.h>
#include <Engine/Layer.h>

#include <exception>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Maho
{
namespace Exception
{

/** Minimal thread-safe multicast event (bind + broadcast). */
template <typename Signature>
class TMulticastEvent;

template <typename... Args>
class TMulticastEvent<void(Args...)>
{
public:
	using FHandler = std::function<void(Args...)>;

	void Bind(FHandler Handler)
	{
		Handlers.push_back(std::move(Handler));
	}

	void Broadcast(Args... Values) const
	{
		for (const auto& Handler : Handlers)
		{
			if (Handler)
			{
				Handler(Values...);
			}
		}
	}

	void RemoveAll()
	{
		Handlers.clear();
	}

private:
	std::vector<FHandler> Handlers;
};

/**
 * Non-fatal exception handling (TSingleton with the fixed lifecycle). Uses:
 *
 *   Exception::FException::Get().OnException.Bind([](const std::string& M) {
 *       Log::Error("exception: {}", M);
 *   });
 *   Exception::FException::Get().ReportException("failed to load texture");
 */
class FException
	: public TSingleton<FException>
	, public IPlugin<IInit, IShutdown>
{
public:
	/** Process-unique accessor — declared here, defined in Exception.cpp (in Maho.dll). */
	static FException& Get();

	void Initialize(int Argc, char** Argv) override;
	void Shutdown() override;

	/** Report a non-fatal exception (broadcasts to OnException). */
	void ReportException(std::string_view Message);

	/** Report from a std::exception (forwards what()). */
	void ReportException(const std::exception& Error);

	/** Subscribers receive every reported exception message. */
	TMulticastEvent<void(const std::string&)> OnException;

protected:
	friend TSingleton<FException>;
	FException() = default;
};

} // namespace Exception
} // namespace Maho
