#pragma once

#include <Core/Interface.h>

namespace Maho
{

/**
 * CRTP singleton — the single-instance mechanism behind Tools. Deriving from
 * TSingleton<TDerived> (which IS ISingleton) gives `static TDerived& Get()`.
 *
 *   class FLog final : public TSingleton<FLog> { ... };
 *   FLog::Get();
 */
template <typename TDerived>
class TSingleton : public ISingleton
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
