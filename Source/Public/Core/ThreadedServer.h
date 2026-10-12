#pragma once


#include <Core/Export.h>

#include <functional>
#include <memory>

namespace Maho
{

class MAHO_API FThreadedServer
{
public:
	FThreadedServer();
	virtual ~FThreadedServer();

	FThreadedServer(const FThreadedServer&) = delete;
	FThreadedServer& operator=(const FThreadedServer&) = delete;

	bool Initialize();

	void Shutdown();

	[[nodiscard]] bool IsRunning() const;

	void Submit(std::function<void()> Task);

	void Flush();

	[[nodiscard]] bool IsServerThread() const;

protected:
	[[nodiscard]] virtual bool OnInitialize();

	virtual void OnShutdown();

	[[nodiscard]] virtual const char* GetThreadName() const;

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};

}
