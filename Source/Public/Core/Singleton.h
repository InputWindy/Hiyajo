#pragma once

namespace Maho
{

/** Singleton identity — a class exposing `static T::Get()` (CRTP singleton). */
class ISingleton
{
public:
	virtual ~ISingleton() = default;
};

/**
 * CRTP singleton — the single process-wide instance of T, created on first
 * access via `static T& Get()` (Meyers). A plugin layer declared as a singleton
 * is driven by FParallelScheduler::ForEachSingletons without an instance array:
 * the type list is traversed level-by-level and each T::Get() is handed to the
 * visitor.
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
