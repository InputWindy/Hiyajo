# Compress - Agent entry

All AI agents read this file before entering this plugin.

## Design constraints (strict)

- Responsibility: byte compression building blocks, a **pure library** - no singleton, no state, only free functions. Do not give it a lifecycle.
- Dependencies only go through `.cplugin` `Dependencies`; include `<Compress.h>`, no cross-directory relative includes.
- Implementation notes:
  - All interfaces live in `Public/Compress.h`; `Private/Compress.cpp` calls the zstd C API.
  - zstd is an engine third-party static library; include paths and linking are provided by the build system - do not configure them on the plugin side.
  - All functions return `std::nullopt` on failure, no exceptions.
- Follow root [AGENTS.md](../../../../AGENTS.md).
