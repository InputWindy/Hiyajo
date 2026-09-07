# Script - Agent Entry

All AI agents must read this file before entering this plugin.

## Purpose

Multi-language script host (`FScriptSystem`) + one backend per language (`IScriptLanguage`).
The host only does language registration / dispatch / lifecycle; VM details, type binding, and script paths all live inside each backend.

## Architecture (strict)

```
FScriptSystem (host, `FScriptSystem::Get()` process-unique)
  `- IScriptLanguage (abstract backend interface, `Script.h`)
       |- FLuaLanguage    - Lua backend (sol2, current default)
       |- FPythonLanguage - Python backend (CPython embed + Scapix bridge, inside TestGame project)
       `- FScriptCSharp   - C# backend (planned: Mono + Scapix)
```

- **Host has zero language knowledge**: `RegisterLanguage(IScriptLanguage*)` registers idempotently by `GetName()`; `GetActive()` returns the first registered backend; `DoFile/Call/LoadScript<>` forward to the active backend.
- **Backends must manage their own lifecycle**: host `Initialize` calls `Initialize(Argc, Argv, "Scripts")` on each; `Shutdown` shuts them down symmetrically.
- **Language-neutral value = opaque `void*`**: `GetState()`/`CallHandle(void*)`/`FTypeBinder = void(*)(void*)` are all opaque; only the caller that knows the language type casts after `include <sol/sol.hpp>` etc.
- **Lua binding macros** (`MAHO_LUA_BIND_BEGIN/FIELD/METHOD_FN/BIND_END` + `MAHO_LUA_BIND_REGISTER`) only generate sol2 code. They are Lua-backend-specific sugar; do not use them for other languages.
- Host does not couple to project logic: script file loading and per-frame `OnUpdate` driving are host/project concerns; FScriptSystem only provides execution primitives.
- Dependencies go only through `.cplugin` `Dependencies`; `Script.h` must stay sol-free (sol2 appears only in `Private/Script.cpp`).

## Known Issues / TODO

### WARNING: Scapix generator does not support Chinese paths (verified 2026-08-26)

- **Symptom**: engine sits at `C:\Users\luchunyi01\Desktop\shujia\Hiyajo` (contains Chinese `shujia`). `scapix.exe` (clang core, precompiled `scapix_bin`) reports `error: no such file or directory` when parsing bridge headers; the Chinese chars are treated as GBK bytes -> generation fails -> MSBuild retries repeatedly, appearing as a "stuck build" (a pile of cmake/python processes with no CPU).
- **Verification**: under an ASCII path (`C:\temp\scapix_test`) scapix.exe generates all python/cs/java/js/objc bridge code fine; the Chinese path fails every time.
- **Impact**: the `ScriptPython` bridge (`scapix_bridge_headers`) cannot build under a Chinese path. Currently `Enabled: false` in `TestGame.cproject`.
- **Candidate fixes**:
  1. Move engine + project to a pure ASCII path (e.g. `C:\Maho`) - complete fix.
  2. In CMake, copy bridge headers to an ASCII temp dir for generation, copy products back - complex.
  3. Drop Scapix, hand-write bindings with pybind11 - lose auto-generation.
- **Re-enable condition**: only after the path becomes ASCII.

### Python backend integration notes (for reference when restoring)

- `ScriptPython.cmake`: cmodule (v2.3.0) -> `find_package(Scapix)` -> two target identities:
  - `ScriptPython.dll` (host, exports `CreateLayer` to the engine, excludes bridge headers)
  - `ScriptPythonBridge.pyd` (Scapix `PYBIND11_MODULE`, `scapix_bridge_headers` as its own target)
- MSVC Debug embedding CPython: `pyconfig.h` under `_DEBUG` pragma-links `pythonXXX_d.lib` (not in the official installer) -> use `target_link_options /NODEFAULTLIB:python<M><m>_d.lib` + temporary `#undef _DEBUG` before including Python.h.
- Host `Initialize` imports the bridge module via `PyImport_ImportModule("ScriptPythonBridge")`; bridge classes land in the `testgame` namespace.

## Docs

- Follow root [AGENTS.md](../../../../AGENTS.md)
