#pragma once

/**
 * UE-style enqueue onto FRenderServer (MahoRender).
 * Usage: ENQUEUE_RENDER_COMMAND(MyCmd)([](FRenderServer& RenderServer) { ... });
 */

#include <Render/RenderServer.h>

#include <Core/System/Log.h>

#include <memory>
#include <type_traits>
#include <utility>

namespace Maho
{
namespace Detail
{

[[nodiscard]] FRenderServer* GetRenderServer();

/** Unique tag per call site (__COUNTER__); TypeName is documentation only. */
template <int UniqueId>
struct TRenderCommandTag
{
};

template <typename TTag, typename TFunc>
inline void EnqueueRenderCommandTagged(TFunc&& Func)
{
	(void)sizeof(TTag);
	FRenderServer* Server = GetRenderServer();
	if (!Server || !Server->IsInitialized())
	{
		MAHO_CORE_ERROR("ENQUEUE_RENDER_COMMAND: FRenderServer unavailable");
		return;
	}

	// std::function requires a copyable callable; wrap the (possibly move-only)
	// user function in a shared_ptr so the inner lambda stays copyable.
	Server->Enqueue(
		[Function = std::make_shared<std::decay_t<TFunc>>(std::forward<TFunc>(Func))](FThreadedServer& Base) mutable
		{
			(*Function)(static_cast<FRenderServer&>(Base));
		});
}

} // namespace Detail
} // namespace Maho

#define ENQUEUE_RENDER_COMMAND(TypeName) \
	::Maho::Detail::EnqueueRenderCommandTagged<::Maho::Detail::TRenderCommandTag<__COUNTER__>>
