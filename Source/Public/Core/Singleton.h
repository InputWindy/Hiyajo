#pragma once

namespace Maho
{

/**
 * Singleton base (CRTP Meyers singleton).
 *
 * A standalone concept — unrelated to the extension system. Anything that
 * wants process-wide single-instance access derives from it:
 *
 *   class FLog final : public TSingleton<FLog> { ... };
 *   FLog::Get();
 */
template <typename TDerived>
class TSingleton
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
