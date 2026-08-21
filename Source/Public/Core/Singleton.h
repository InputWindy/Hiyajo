#pragma once

namespace Maho
{

/** Identity marker: a class is a singleton (static T::Get() access). */
class ISingleton
{
public:
	virtual ~ISingleton() = default;
};

/**
 * CRTP singleton — the single-instance mechanism behind Tools. Deriving from
 * TSingleton<TDerived> gives `static TDerived& Get()`.
 *
 *   class FLog final : public TSingleton<FLog> { ... };
 *   FLog::Get();
 */
template <typename TDerived>
class TSingleton: public ISingleton
{
protected:
	TSingleton() = default;

public:
	virtual ~TSingleton() = default;

	TSingleton(const TSingleton&) = delete;
	TSingleton& operator=(const TSingleton&) = delete;

	static TDerived& Get()
	{
		static TDerived Instance;
		return Instance;
	}
};

} // namespace Maho
