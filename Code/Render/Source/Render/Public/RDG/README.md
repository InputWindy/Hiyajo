# RDG

Render dependency graph (frame graph) module.

- `RDGBuilder.h` — graph build / compile / execute.
- `RDGPass.h` — pass descriptor + execution callback.
- `RDGResources.h` — transient/external buffer + texture views.
- `RDGTransientPool.h` — pooled transient allocations.

Use `RDGBuilder` through the aggregate `Render.h`; see [Render module](../README.md) and [RHI](../RHI/CONTRACT.md).
