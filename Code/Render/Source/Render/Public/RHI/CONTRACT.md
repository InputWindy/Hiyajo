# RHI — module contract

## Role

Backend-agnostic GPU device surface for Maho: resources, command recording, and three **logical** queues (Graphics / Compute / Transfer).

## Invariants

- Public headers under this folder: **no** `vulkan.h`, **no** VMA types.
- Upper layers use `FRHIResourceManager::Acquire*` / `Release` (not ad-hoc `IRHI::Create*` as the normal path).
- `GetGraphicsQueue()` / `GetComputeQueue()` / `GetTransferQueue()` always return valid references.
- `FRHIQueue::GetType()` is the **logical** type (Transfer stays Transfer even when native queue is Graphics).
- `IsNativeFallback()` is debug/logging only — upper code must not require it.
- Call RHI resource/command APIs on the RHI thread (`FRHIServer::Enqueue`) unless a future doc says otherwise.

## Allowed callers

- Engine render stages / `FRHIServer` / future upload systems
- Tests / samples that only include Public RHI headers

## Forbidden

- `#include <vulkan/vulkan.h>` or `vk_mem_alloc.h` from Public or game code
- Assuming a dedicated DMA/transfer hardware queue exists
- Hand-editing Generated sources to “wire” RHI

## Threading

- Device/Manager/Submit: RHI thread (`MahoRHI`)
- Game thread talks to RHI via `FRHIServer` submit helpers / `Enqueue`

## Status

- **Done:** Public API surface; Manager; VMA; three logical queues + pools; command list skeleton; existing clear + ImGui still run.
- **Gap:** Real graphics/compute PSO; polished texture copy; ImGui still on `GetVk*`.

## Pitfalls

- Win32: use `CreateGpuSemaphore` / `DestroyGpuSemaphore` (not `CreateSemaphore`).
- `GetVkGraphicsQueue()` ≠ `IRHI::GetGraphicsQueue()` (`VkQueue` vs `FRHIQueue&`).
- External Unreal study notes are not this RHI; trust this CONTRACT + `DESIGN_JOURNAL.md`

## Related files

- Public: `RHI.h`, `RHIEnums.h`, `RHIResources.h`, `RHICommandList.h`, `RHIResourceManager.h`
- Private: `VulkanRHI.*`, `VulkanMemory.*`, `VulkanCommandList.*`, `VulkanResources.*`
- Journal: [`../../../../Doc/Engine/DESIGN_JOURNAL.md`](../../../../Doc/Engine/DESIGN_JOURNAL.md) (from repo root: `Doc/Engine/DESIGN_JOURNAL.md`)
