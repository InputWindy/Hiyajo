#pragma once


#include <Core/Export.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace Maho
{

class MAHO_API FThreadPool
{
public:
	using FLane = std::uint32_t;

	static constexpr FLane         DefaultLane = 0;
	static constexpr FLane         InvalidLane = 0xFFFFFFFFu;
	static constexpr std::uint32_t MaxLanes    = 16;

	explicit FThreadPool(std::uint32_t NumThreads = 0);
	~FThreadPool();

	FThreadPool(const FThreadPool&) = delete;
	FThreadPool& operator=(const FThreadPool&) = delete;

	void Submit(std::function<void()> Task);

	void Flush();

	[[nodiscard]] FLane CreateLane();

	void DestroyLane(FLane Lane);

	void Submit(FLane Lane, std::function<void()> Task);

	void Flush(FLane Lane);

	[[nodiscard]] std::uint32_t GetNumThreads() const;

	[[nodiscard]] std::uint32_t GetLanePending(FLane Lane) const;

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};

}
