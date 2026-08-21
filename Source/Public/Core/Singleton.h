#pragma once

#include <type_traits>

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

/**
 * True when T is a CRTP singleton (derives from TSingleton<T> itself).
 *
 *   static_assert(TIsSingleton<FLog>::value);
 */
template <typename T, typename = void>
struct TIsSingleton : std::false_type
{
};

template <typename T>
struct TIsSingleton<T, std::enable_if_t<std::is_base_of_v<TSingleton<T>, T>>>
	: std::true_type
{
};

template <typename T>
inline constexpr bool TIsSingleton_v = TIsSingleton<T>::value;

} // namespace Maho
