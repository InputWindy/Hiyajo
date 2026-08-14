#pragma once

/**
 * Intrusive reference counting base class (UE-style TRefCounted).
 * T must derive from TRefCounted<T>. Use Ref<T> smart pointer to manage.
 */

#include <Core/Misc/Export.h>

#include <atomic>
#include <cstdint>

namespace Maho
{

template <typename T>
class Ref;

template <typename T>
class TRefCounted
{
public:
	TRefCounted() = default;
	virtual ~TRefCounted() = default;

	TRefCounted(const TRefCounted&) = delete;
	TRefCounted& operator=(const TRefCounted&) = delete;

	[[nodiscard]] std::uint32_t GetRefCount() const { return RefCount.load(std::memory_order_acquire); }

private:
	friend class Ref<T>;

	std::uint32_t AddRef() const
	{
		return RefCount.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	std::uint32_t Release() const
	{
		const std::uint32_t R = RefCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
		if (R == 0)
		{
			delete static_cast<const T*>(this);
		}
		return R;
	}

	mutable std::atomic<std::uint32_t> RefCount{0};
};

/**
 * Intrusive smart pointer (light shared_ptr alternative).
 * T must derive from TRefCounted<T>.
 */
template <typename T>
class Ref
{
public:
	Ref() = default;
	Ref(std::nullptr_t) : Ptr(nullptr) {}

	explicit Ref(T* InPtr) : Ptr(InPtr)
	{
		if (Ptr)
			Ptr->AddRef();
	}

	Ref(const Ref& Other) : Ptr(Other.Ptr)
	{
		if (Ptr)
			Ptr->AddRef();
	}

	Ref(Ref&& Other) noexcept : Ptr(Other.Ptr)
	{
		Other.Ptr = nullptr;
	}

	~Ref()
	{
		if (Ptr)
			Ptr->Release();
	}

	Ref& operator=(const Ref& Other)
	{
		if (this != &Other)
		{
			if (Ptr) Ptr->Release();
			Ptr = Other.Ptr;
			if (Ptr) Ptr->AddRef();
		}
		return *this;
	}

	Ref& operator=(Ref&& Other) noexcept
	{
		if (this != &Other)
		{
			if (Ptr) Ptr->Release();
			Ptr = Other.Ptr;
			Other.Ptr = nullptr;
		}
		return *this;
	}

	Ref& operator=(T* InPtr)
	{
		if (Ptr) Ptr->Release();
		Ptr = InPtr;
		if (Ptr) Ptr->AddRef();
		return *this;
	}

	[[nodiscard]] T* Get() const { return Ptr; }
	[[nodiscard]] T* operator->() const { return Ptr; }
	[[nodiscard]] T& operator*() const { return *Ptr; }
	[[nodiscard]] bool IsValid() const { return Ptr != nullptr; }
	[[nodiscard]] explicit operator bool() const { return Ptr != nullptr; }

	[[nodiscard]] bool operator==(const Ref& Other) const { return Ptr == Other.Ptr; }
	[[nodiscard]] bool operator!=(const Ref& Other) const { return Ptr != Other.Ptr; }

private:
	T* Ptr = nullptr;
};

} // namespace Maho
