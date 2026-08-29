# Text - Agent Entry

All AI agents must read this file before entering this plugin.

## Design Constraints (strict)

- Responsibility boundary: localization. User-facing display strings become `FText`; do not scatter hard-coded literals. All UI copy goes through the `FTextManager` catalog.
- Dependencies go only through `.cplugin` `Dependencies`; include `<Text.h>`, no cross-directory relative includes.
- Implementation notes:
  - `TSingleton<FTextManager>`, `Get()` defined in `Private/Text.cpp` (process-unique inside Text.dll).
  - Catalog is thread-safe (mutex); `FText::Resolve` looks up the current culture and falls back to Source when missing.
  - JSON translation format: `[{ "Namespace", "Key", "Culture", "Text" }, ...]`.
- Follow root [AGENTS.md](../../../../AGENTS.md).
