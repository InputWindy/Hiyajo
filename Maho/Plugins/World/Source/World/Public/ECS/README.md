# ECS

Entity-component-system data model.

- `World.h` — top-level entity/component container.
- `EntityManager.h` — archetype storage + entity lifecycle.
- `Archetype.h` / `Chunk.h` — column storage.
- `ComponentType.h` — runtime component type registry.
- `Query.h` — component queries.
- `System.h` / `SystemGroup.h` — system lifecycle + groups.
- `EntityCommandBuffer.h` — deferred structural changes.
- `DOTS.h` — component registration macros.

Aggregate: `ECS.h`. See the [World plugin README](../../../../README.md).
