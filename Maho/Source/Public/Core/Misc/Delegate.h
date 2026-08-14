#pragma once

/**
 * UE-style unicast / multicast delegates (header-only).
 *
 * Scope (v1):
 *   - TDelegate<Ret(Args...)>           unicast: one binding, Execute / ExecuteIfBound
 *   - TMulticastDelegate<void(Args...)> multicast: many bindings, Broadcast / Remove by handle
 *   - BindStatic / BindRaw / BindLambda (and Add* for multicast)
 *
 * Not in v1 (add later if needed):
 *   - BindUObject / weak object (needs TWeakObjectPtr)
 *   - BindSP / shared-ptr lifetime
 *   - thread-safe multicast
 *
 * Example:
 * ```
 *   struct FUser
 *   {
 *       int Value = 0;
 *       void Add(int X) { Value += X; }
 *       int Scale(int X) const { return Value * X; }
 *   };
 *
 *   FUser User;
 *   User.Value = 2;
 *
 *   // Unicast
 *   Maho::TDelegate<int(int)> OnScale;
 *   OnScale.BindRaw(&User, &FUser::Scale);
 *   const int Scaled = OnScale.Execute(3); // 6
 *
 *   // Multicast
 *   Maho::TMulticastDelegate<void(int)> OnAdd;
 *   const Maho::FDelegateHandle H = OnAdd.AddRaw(&User, &FUser::Add);
 *   OnAdd.AddLambda([&User](int X) { User.Value += X; });
 *   OnAdd.Broadcast(5);
 *   OnAdd.Remove(H);
 *   OnAdd.RemoveAll(&User);
 *
 *   // Or declare a named type:
 *   MAHO_DECLARE_DELEGATE_OneParam(FOnInt, int);
 *   MAHO_DECLARE_MULTICAST_DELEGATE_OneParam(FOnIntMulti, int);
 *   FOnInt A;
 *   FOnIntMulti B;
 * ```
 */

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

namespace Maho
{

/**
 * Opaque id returned by TMulticastDelegate::Add*.
 * Compare / Remove with the same multicast instance that issued it.
 */
struct FDelegateHandle
{
	std::uint64_t Id = 0;

	[[nodiscard]] bool IsValid() const { return Id != 0; }
	void Reset() { Id = 0; }

	[[nodiscard]] bool operator==(const FDelegateHandle& Other) const { return Id == Other.Id; }
	[[nodiscard]] bool operator!=(const FDelegateHandle& Other) const { return Id != Other.Id; }
};

namespace DelegatePrivate
{

[[nodiscard]] inline std::uint64_t NextHandleId()
{
	static std::atomic<std::uint64_t> Counter{1};
	return Counter.fetch_add(1, std::memory_order_relaxed);
}

} // namespace DelegatePrivate

/**
 * Unicast delegate: at most one binding.
 * Binding again replaces the previous one.
 */
template <typename TSignature>
class TDelegate;

template <typename TRet, typename... TArgs>
class TDelegate<TRet(TArgs...)>
{
public:
	using FSignature = TRet(TArgs...);
	using FFunction = std::function<FSignature>;

	TDelegate() = default;

	TDelegate(const TDelegate&) = default;
	TDelegate& operator=(const TDelegate&) = default;
	TDelegate(TDelegate&&) noexcept = default;
	TDelegate& operator=(TDelegate&&) noexcept = default;

	void Unbind()
	{
		Function = nullptr;
	}

	[[nodiscard]] bool IsBound() const
	{
		return static_cast<bool>(Function);
	}

	explicit operator bool() const
	{
		return IsBound();
	}

	void BindLambda(FFunction InFunction)
	{
		Function = std::move(InFunction);
	}

	template <typename TFunctor>
	void BindLambda(TFunctor&& Functor)
	{
		Function = FFunction(std::forward<TFunctor>(Functor));
	}

	void BindStatic(TRet (*InFunction)(TArgs...))
	{
		Function = InFunction;
	}

	template <typename TUser>
	void BindRaw(TUser* UserObject, TRet (TUser::*Method)(TArgs...))
	{
		Function = [UserObject, Method](TArgs... Args) -> TRet
		{
			return (UserObject->*Method)(std::forward<TArgs>(Args)...);
		};
	}

	template <typename TUser>
	void BindRaw(const TUser* UserObject, TRet (TUser::*Method)(TArgs...) const)
	{
		Function = [UserObject, Method](TArgs... Args) -> TRet
		{
			return (UserObject->*Method)(std::forward<TArgs>(Args)...);
		};
	}

	TRet Execute(TArgs... Args) const
	{
		return Function(std::forward<TArgs>(Args)...);
	}

	/**
	 * Invoke if bound.
	 * void: returns whether it ran.
	 * non-void: writes result to OutResult when bound; returns whether it ran.
	 */
	template <typename T = TRet, typename = std::enable_if_t<std::is_void_v<T>>>
	bool ExecuteIfBound(TArgs... Args) const
	{
		if (!IsBound())
		{
			return false;
		}
		Function(std::forward<TArgs>(Args)...);
		return true;
	}

	template <typename T = TRet, typename = std::enable_if_t<!std::is_void_v<T>>>
	bool ExecuteIfBound(T& OutResult, TArgs... Args) const
	{
		if (!IsBound())
		{
			return false;
		}
		OutResult = Function(std::forward<TArgs>(Args)...);
		return true;
	}

private:
	FFunction Function;
};

/**
 * Multicast delegate: zero or more void(Args...) bindings.
 * Broadcast copies the invocation list so Add/Remove during Broadcast is safe
 * for the current pass (new adds are not called until the next Broadcast).
 */
template <typename TSignature>
class TMulticastDelegate;

template <typename... TArgs>
class TMulticastDelegate<void(TArgs...)>
{
public:
	using FSignature = void(TArgs...);
	using FFunction = std::function<FSignature>;

	TMulticastDelegate() = default;

	TMulticastDelegate(const TMulticastDelegate&) = default;
	TMulticastDelegate& operator=(const TMulticastDelegate&) = default;
	TMulticastDelegate(TMulticastDelegate&&) noexcept = default;
	TMulticastDelegate& operator=(TMulticastDelegate&&) noexcept = default;

	void Clear()
	{
		Bindings.clear();
	}

	[[nodiscard]] bool IsBound() const
	{
		return !Bindings.empty();
	}

	[[nodiscard]] std::size_t GetNumBindings() const
	{
		return Bindings.size();
	}

	[[nodiscard]] FDelegateHandle AddLambda(FFunction InFunction)
	{
		return AddInternal(std::move(InFunction));
	}

	template <typename TFunctor>
	[[nodiscard]] FDelegateHandle AddLambda(TFunctor&& Functor)
	{
		return AddInternal(FFunction(std::forward<TFunctor>(Functor)));
	}

	[[nodiscard]] FDelegateHandle AddStatic(void (*InFunction)(TArgs...))
	{
		return AddInternal(FFunction(InFunction));
	}

	template <typename TUser>
	[[nodiscard]] FDelegateHandle AddRaw(TUser* UserObject, void (TUser::*Method)(TArgs...))
	{
		FBinding Binding;
		Binding.Handle.Id = DelegatePrivate::NextHandleId();
		Binding.RawObject = UserObject;
		Binding.RawMethod = DetailMethodKey(Method);
		Binding.Function = [UserObject, Method](TArgs... Args)
		{
			(UserObject->*Method)(std::forward<TArgs>(Args)...);
		};
		Bindings.push_back(std::move(Binding));
		return Bindings.back().Handle;
	}

	template <typename TUser>
	[[nodiscard]] FDelegateHandle AddRaw(const TUser* UserObject, void (TUser::*Method)(TArgs...) const)
	{
		FBinding Binding;
		Binding.Handle.Id = DelegatePrivate::NextHandleId();
		Binding.RawObject = const_cast<TUser*>(UserObject);
		Binding.RawMethod = DetailMethodKey(Method);
		Binding.Function = [UserObject, Method](TArgs... Args)
		{
			(UserObject->*Method)(std::forward<TArgs>(Args)...);
		};
		Bindings.push_back(std::move(Binding));
		return Bindings.back().Handle;
	}

	/** Add if no existing binding shares the same handle id (always unique) — prefer AddUniqueRaw. */
	template <typename TUser>
	[[nodiscard]] FDelegateHandle AddUniqueRaw(TUser* UserObject, void (TUser::*Method)(TArgs...))
	{
		for (const FBinding& Binding : Bindings)
		{
			if (Binding.RawObject == UserObject && Binding.RawMethod == DetailMethodKey(Method))
			{
				return Binding.Handle;
			}
		}

		FBinding Binding;
		Binding.Handle.Id = DelegatePrivate::NextHandleId();
		Binding.RawObject = UserObject;
		Binding.RawMethod = DetailMethodKey(Method);
		Binding.Function = [UserObject, Method](TArgs... Args)
		{
			(UserObject->*Method)(std::forward<TArgs>(Args)...);
		};
		Bindings.push_back(std::move(Binding));
		return Bindings.back().Handle;
	}

	bool Remove(FDelegateHandle Handle)
	{
		if (!Handle.IsValid())
		{
			return false;
		}

		for (auto It = Bindings.begin(); It != Bindings.end(); ++It)
		{
			if (It->Handle == Handle)
			{
				Bindings.erase(It);
				return true;
			}
		}
		return false;
	}

	template <typename TUser>
	void RemoveAll(TUser* UserObject)
	{
		if (UserObject == nullptr)
		{
			return;
		}

		Bindings.erase(
			std::remove_if(
				Bindings.begin(),
				Bindings.end(),
				[UserObject](const FBinding& Binding)
				{
					return Binding.RawObject == UserObject;
				}),
			Bindings.end());
	}

	void Broadcast(TArgs... Args) const
	{
		// Copy so listeners may Add/Remove safely during this Broadcast.
		const std::vector<FBinding> Local = Bindings;
		for (const FBinding& Binding : Local)
		{
			if (Binding.Function)
			{
				Binding.Function(Args...);
			}
		}
	}

private:
	struct FBinding
	{
		FDelegateHandle Handle;
		FFunction Function;
		void* RawObject = nullptr;
		std::uintptr_t RawMethod = 0;
	};

	template <typename TMethod>
	[[nodiscard]] static std::uintptr_t DetailMethodKey(TMethod Method)
	{
		static_assert(sizeof(TMethod) <= sizeof(std::uintptr_t) * 2, "Member pointer too large for AddUniqueRaw key");
		std::uintptr_t Words[2] = {0, 0};
		std::memcpy(Words, &Method, sizeof(TMethod));
		return Words[0] ^ Words[1];
	}

	[[nodiscard]] FDelegateHandle AddInternal(FFunction InFunction)
	{
		FBinding Binding;
		Binding.Handle.Id = DelegatePrivate::NextHandleId();
		Binding.Function = std::move(InFunction);
		Bindings.push_back(std::move(Binding));
		return Bindings.back().Handle;
	}

	std::vector<FBinding> Bindings;
};

} // namespace Maho

// ---------------------------------------------------------------------------
// MAHO_DECLARE_* macros — prefer these for named delegate types.
// Do not write `using FOnX = TMulticastDelegate<...>` / `TDelegate<...>` at call sites.
//
//   MAHO_DECLARE_DELEGATE(FOnReady);
//   MAHO_DECLARE_DELEGATE_OneParam(FOnScore, int);
//   MAHO_DECLARE_DELEGATE_RetVal_TwoParams(bool, FOnQuery, int, float);
//   MAHO_DECLARE_MULTICAST_DELEGATE_OneParam(FOnChanged, int);
//   MAHO_DECLARE_MULTICAST_DELEGATE_OneParam(FStageMulticast, EEngineStage);
// ---------------------------------------------------------------------------

#define MAHO_DECLARE_DELEGATE(DelegateName) \
	using DelegateName = ::Maho::TDelegate<void()>

#define MAHO_DECLARE_DELEGATE_RetVal(RetType, DelegateName) \
	using DelegateName = ::Maho::TDelegate<RetType()>

#define MAHO_DECLARE_DELEGATE_OneParam(DelegateName, Param1Type) \
	using DelegateName = ::Maho::TDelegate<void(Param1Type)>

#define MAHO_DECLARE_DELEGATE_RetVal_OneParam(RetType, DelegateName, Param1Type) \
	using DelegateName = ::Maho::TDelegate<RetType(Param1Type)>

#define MAHO_DECLARE_DELEGATE_TwoParams(DelegateName, Param1Type, Param2Type) \
	using DelegateName = ::Maho::TDelegate<void(Param1Type, Param2Type)>

#define MAHO_DECLARE_DELEGATE_RetVal_TwoParams(RetType, DelegateName, Param1Type, Param2Type) \
	using DelegateName = ::Maho::TDelegate<RetType(Param1Type, Param2Type)>

#define MAHO_DECLARE_DELEGATE_ThreeParams(DelegateName, Param1Type, Param2Type, Param3Type) \
	using DelegateName = ::Maho::TDelegate<void(Param1Type, Param2Type, Param3Type)>

#define MAHO_DECLARE_DELEGATE_RetVal_ThreeParams(RetType, DelegateName, Param1Type, Param2Type, Param3Type) \
	using DelegateName = ::Maho::TDelegate<RetType(Param1Type, Param2Type, Param3Type)>

#define MAHO_DECLARE_DELEGATE_FourParams(DelegateName, Param1Type, Param2Type, Param3Type, Param4Type) \
	using DelegateName = ::Maho::TDelegate<void(Param1Type, Param2Type, Param3Type, Param4Type)>

#define MAHO_DECLARE_MULTICAST_DELEGATE(DelegateName) \
	using DelegateName = ::Maho::TMulticastDelegate<void()>

#define MAHO_DECLARE_MULTICAST_DELEGATE_OneParam(DelegateName, Param1Type) \
	using DelegateName = ::Maho::TMulticastDelegate<void(Param1Type)>

#define MAHO_DECLARE_MULTICAST_DELEGATE_TwoParams(DelegateName, Param1Type, Param2Type) \
	using DelegateName = ::Maho::TMulticastDelegate<void(Param1Type, Param2Type)>

#define MAHO_DECLARE_MULTICAST_DELEGATE_ThreeParams(DelegateName, Param1Type, Param2Type, Param3Type) \
	using DelegateName = ::Maho::TMulticastDelegate<void(Param1Type, Param2Type, Param3Type)>

#define MAHO_DECLARE_MULTICAST_DELEGATE_FourParams(DelegateName, Param1Type, Param2Type, Param3Type, Param4Type) \
	using DelegateName = ::Maho::TMulticastDelegate<void(Param1Type, Param2Type, Param3Type, Param4Type)>
