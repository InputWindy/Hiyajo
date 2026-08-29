# Archive - Agent entry

All AI agents read this file before entering this plugin.

## Design constraints (strict)

- Responsibility: binary serialization building blocks, a **pure library** - no singleton, no state, just free classes + free functions. Do not give it a lifecycle.
- Dependencies only go through `.cplugin` `Dependencies`; include `<Archive.h>`, no cross-directory relative includes.
- Implementation notes:
  - All interfaces live in `Public/Archive.h` (header-only); `Private/Archive.cpp` only implements non-template methods.
  - `FMemoryReader` references an external buffer without copying - mind the lifetime.
  - The generic `operator<<` template has `static_assert(std::is_trivially_copyable_v<T>)`; non-POD types go through `ISerialize`.
- Follow root [AGENTS.md](../../../../AGENTS.md).
