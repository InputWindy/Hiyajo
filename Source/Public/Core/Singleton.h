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

	/** Bring the singleton up — the fixed init phase. */
	virtual void Initiate() = 0;

	/** Tear it down — the fixed shutdown phase. */
	virtual void Shutdown() = 0;
};

/**
 * CRTP singleton — the single process-wide instance of T, created on first
 * access via `static T& Get()` (Meyers). A derived singleton only needs to
 * implement Initiate/Shutdown.
 */
template <typename T>
class TSingleton : public ISingleton
{
public:
	static T& Get()
	{
		static T Instance;
		return Instance;
	}

protected:
	TSingleton() = default;
	~TSingleton() override = default;

	TSingleton(const TSingleton&) = delete;
	TSingleton& operator=(const TSingleton&) = delete;
};

} // namespace Maho
