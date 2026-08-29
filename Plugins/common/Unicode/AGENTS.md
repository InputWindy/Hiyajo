# Unicode - Agent Entry

All AI agents must read this file before entering this plugin.

## Design Constraints (strict)

- Responsibility boundary: encoding conversion building blocks, **pure library** - no singleton, no state, free functions. Do not give it a lifecycle.
- Engine internals are always UTF-8 `std::string`; use `ToNative/FromNative` only at platform boundaries (Windows API calls, file paths); do not transcode everywhere.
- Dependencies go only through `.cplugin` `Dependencies`; include `<Unicode.h>`, no cross-directory relative includes.
- Implementation notes:
  - All interfaces in `Public/Unicode.h`, implementation in `Private/Unicode.cpp`; on Windows convert via WinAPI to UTF-16, other platforms passthrough.
  - `EnsureConsoleUtf8` only takes effect on Windows.
- Follow root [AGENTS.md](../../../../AGENTS.md).
