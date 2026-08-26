#pragma once

namespace Maho
{

/**
 * CRTP singleton — the process-wide single instance of T. **Identity/flag base
 * only**: no inline Meyers, no forced lifecycle. Each derived singleton declares
 * `static T& Get();` itself and defines it in its own .cpp (compiled into its
 * DLL), so the instance lives in exactly one translation unit of one DLL →
 * process-unique across DLL boundaries (an inline static local here would be
 * duplicated per include-site DLL). `is_base_of_v<TSingleton<T>, T>` still
 * identifies a singleton (query traversal unchanged). Lifecycle capabilities
 * (IInit / IShutdown, from Core/Interface.h) are composed via `IPlugin` when a
 * singleton needs them — not inherited unconditionally.
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

