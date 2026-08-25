#pragma once

namespace Maho
{

/**
 * Build-time only capability interface — "this singleton can be brought up"
 * (the fixed Initiate phase). A singleton that needs a lifecycle composes it
 * explicitly, e.g. `TSingleton<FLog>, IPlugin<IInitialize, IShutdown>`. Services
 * that are pure getters / never torn down simply omit both.
 */
class IInitialize
{
public:
	virtual ~IInitialize() = default;

	/** Bring the singleton up — the fixed init phase (receives launch args). */
	virtual void Initiate(int Argc, char** Argv) = 0;
};

/** Build-time only capability interface — "this singleton can be torn down". */
class IShutdown
{
public:
	virtual ~IShutdown() = default;

	/** Tear it down — the fixed shutdown phase. */
	virtual void Shutdown() = 0;
};

/**
 * CRTP singleton — the process-wide single instance of T. **Identity/flag base
 * only**: no inline Meyers, no forced lifecycle. Each derived singleton declares
 * `static T& Get();` itself and defines it in its own .cpp (compiled into its
 * DLL), so the instance lives in exactly one translation unit of one DLL →
 * process-unique across DLL boundaries (an inline static local here would be
 * duplicated per include-site DLL). `is_base_of_v<TSingleton<T>, T>` still
 * identifies a singleton (query traversal unchanged). Lifecycle interfaces
 * (IInitialize / IShutdown) are composed via `IPlugin` when a singleton needs
 * them — not inherited unconditionally.
 */
template <typename T>
class TSingleton
{
protected:
	TSingleton() = default;
	~TSingleton() = default;

	TSingleton(const TSingleton&) = delete;
	TSingleton& operator=(const TSingleton&) = delete;
};

} // namespace Maho

