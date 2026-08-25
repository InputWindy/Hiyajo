#pragma once

namespace Maho
{

/**
 * Singleton identity + fixed lifecycle — a class exposing `static T::Get()`
 * (CRTP singleton) with two lifecycle hooks: Initiate (bring it up) and
 * Shutdown (tear it down). The engine drives these the same way for every
 * singleton plugin via Select<ISingleton>().ForEach.
 */
class ISingleton
{
public:
	virtual ~ISingleton() = default;

	/** Bring the singleton up — the fixed init phase (receives launch args). */
	virtual void Initiate(int Argc, char** Argv) = 0;

	/** Tear it down — the fixed shutdown phase. */
	virtual void Shutdown() = 0;
};

/**
 * CRTP singleton — the process-wide single instance of T. **Identity/flag base
 * only**: no inline Meyers here. Each derived singleton declares `static T&
 * Get();` itself and defines it in its own .cpp (compiled into its DLL), so the
 * instance lives in exactly one translation unit of one DLL → process-unique
 * across DLL boundaries (an inline static local here would be duplicated per
 * include-site DLL). `is_base_of_v<TSingleton<T>, T>` still identifies a
 * singleton (query traversal unchanged).
 */
template <typename T>
class TSingleton : public ISingleton
{
protected:
	TSingleton() = default;
	~TSingleton() override = default;

	TSingleton(const TSingleton&) = delete;
	TSingleton& operator=(const TSingleton&) = delete;
};

} // namespace Maho
