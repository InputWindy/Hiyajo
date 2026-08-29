# Timer - Agent Entry

All AI agents must read this file before entering this plugin.

## Design Constraints (strict)

- Responsibility boundary: performance timing + game clock. Use `FScopedTimer` for hot functions; use `FGameClock` for game logic time; do not hand-roll chrono.
- Dependencies go only through `.cplugin` `Dependencies`; include `<Timer.h>`, no cross-directory relative includes.
- Implementation notes:
  - **Two singletons**: `FTimer` and `FGameClock`; both `Get()` definitions live in `Private/Timer.cpp` (process-unique inside Timer.dll).
  - `FScopedTimer` is stack-scope timing; must pair `BeginScope/EndScope`.
  - `FGameClock` advances lazily - `GetGameSeconds()` accumulates wall-clock delta since the last advance x TimeScale; no per-frame call required.
- Follow root [AGENTS.md](../../../../AGENTS.md).
