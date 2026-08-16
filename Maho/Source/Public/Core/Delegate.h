#pragma once

#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

namespace Maho
{

/**
 * Handle to one multicast binding (returned by Add / AddRaw).
 * Only meaningful within the delegate instance that produced it.
 */
struct FDelegateHandle
{
	std::uint64_t Id = 0;

	[[nodiscard]] bool operator==(const FDelegateHandle& Other) const
	{
		return Id == Other.Id;
	}
	[[nodiscard]] bool operator!=(const FDelegateHandle& Other) const
	{
		return Id != Other.Id;
	}
	[[nodiscard]] explicit operator bool() const
	{
		return Id != 0;
	}
};

/**
 * Unicast delegate — exactly one binding, may return a value.
 * Use when a callback produces a result (query / factory style).
 *
 *   TDelegate<int(int)> OnScale;
 *   OnScale.Bind([](int X) { return X * 2; });
 *   OnScale.BindRaw(&Obj, &FObj::Scale);
 *   const int V = OnScale.Execute(3);
 *   const int W = OnScale.ExecuteIfBound(3);   // returns 0 when unbound
 */
template <typename TSignature>
class TDelegate; // primary undefined — enforces a function signature

template <typename Ret, typename... TArgs>
class TDelegate<Ret(TArgs...)>
{
public:
	using FFunction = std::function<Ret(TArgs...)>;

	TDelegate() = default;

	/** Bind a callable (lambda / static / functor). */
	void Bind(FFunction InFunction)
	{
		Fn = std::move(InFunction);
	}

	/** Bind a member function on Object. */
	template <typename TObject, typename TMethod>
	void BindRaw(TObject* Object, TMethod Method)
	{
		Fn = [Object, Method](TArgs... Args) -> Ret
		{
			return (Object->*Method)(Args...);
		};
	}

	void Unbind()
	{
		Fn = nullptr;
	}

	[[nodiscard]] bool IsBound() const
	{
		return static_cast<bool>(Fn);
	}

	/** Invoke the binding. Call only when IsBound() — otherwise UB. */
	Ret Execute(TArgs... Args) const
	{
		return Fn(std::forward<TArgs>(Args)...);
	}

	/** Invoke when bound; returns Ret{} when unbound. */
	Ret ExecuteIfBound(TArgs... Args) const
	{
		if (Fn)
		{
			return Fn(std::forward<TArgs>(Args)...);
		}
		if constexpr (std::is_void_v<Ret>)
		{
			return;
		}
		else
		{
			return Ret{};
		}
	}

private:
	FFunction Fn;
};

/**
 * Void multicast delegate — many bindings, no return value.
 * One type serves both unicast (Add one binding) and multicast (Add many).
 *
 *   - Add(Function)   lambda / static / functor
 *   - AddRaw(Object, &T::Method)   member function; the object pointer is
 *                                  tracked so RemoveAll(Object) can unbind it
 *
 * Broadcast snapshots the binding list, so a listener may Add / Remove during
 * Broadcast without invalidating iteration. Not thread-safe.
 */
template <typename TSignature>
class TMulticastDelegate; // primary is undefined — enforces void return

template <typename... TArgs>
class TMulticastDelegate<void(TArgs...)>
{
public:
	using FFunction = std::function<void(TArgs...)>;

	TMulticastDelegate() = default;

	/** Add a callable (lambda / static / functor). */
	FDelegateHandle Add(FFunction Function)
	{
		return AddInternal(nullptr, std::move(Function));
	}

	/** Add a member function on Object (tracked for RemoveAll). */
	template <typename TObject, typename TMethod>
	FDelegateHandle AddRaw(TObject* Object, TMethod Method)
	{
		return AddInternal(
			static_cast<void*>(Object),
			[Object, Method](TArgs... Args)
			{
				(Object->*Method)(Args...);
			});
	}

	/** Remove one binding by handle. */
	bool Remove(FDelegateHandle Handle)
	{
		for (auto It = Bindings.begin(); It != Bindings.end(); ++It)
		{
			if (It->Handle.Id == Handle.Id)
			{
				Bindings.erase(It);
				return true;
			}
		}
		return false;
	}

	/** Remove every binding attached to Object (AddRaw bindings only). */
	int RemoveAll(void* Object)
	{
		if (Object == nullptr)
		{
			return 0;
		}
		int Removed = 0;
		for (auto It = Bindings.begin(); It != Bindings.end();)
		{
			if (It->Object == Object)
			{
				It = Bindings.erase(It);
				++Removed;
			}
			else
			{
				++It;
			}
		}
		return Removed;
	}

	/** Broadcast to every binding (snapshot — mutation during broadcast is safe). */
	void Broadcast(TArgs... Args) const
	{
		const std::vector<FBinding> Snapshot = Bindings;
		for (const FBinding& Binding : Snapshot)
		{
			if (Binding.Function)
			{
				Binding.Function(Args...);
			}
		}
	}

	void Clear()
	{
		Bindings.clear();
	}

	[[nodiscard]] bool IsBound() const
	{
		return !Bindings.empty();
	}

	[[nodiscard]] int Num() const
	{
		return static_cast<int>(Bindings.size());
	}

private:
	struct FBinding
	{
		FDelegateHandle Handle;
		void* Object = nullptr;
		FFunction Function;
	};

	FDelegateHandle AddInternal(void* Object, FFunction Function)
	{
		const FDelegateHandle Handle{++NextHandleId};
		Bindings.push_back(FBinding{Handle, Object, std::move(Function)});
		return Handle;
	}

	std::vector<FBinding> Bindings;
	std::uint64_t NextHandleId = 0;
};

} // namespace Maho
