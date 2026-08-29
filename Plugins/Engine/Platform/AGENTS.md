# Platform - Agent Entry

All AI agents must read this file before entering this plugin.

## Design Constraints (strict)

- Responsibility boundary: provide **native surface + events** to RHI - desktop window (GLFW) and Linux headless context (EGL pbuffer). Window semantics are hidden behind the single `IPlatform::GetNativeWindow()` accessor - windowless platforms (headless / Android surface / iOS view) do not expose the window concept.
- Dependencies only go through `.cplugin` `Dependencies` (currently `[]`, engine core is auto-linked via codegen); include `<Platform.h>`, no cross-directory relative includes.
- Implementation notes:
  - `TSingleton` + `IPlugin<IInit, IShutdown>`, `Get()` is defined in `Private/Platform.cpp` (process-unique inside Platform.dll, Meyers singleton).
  - backend is selected per platform: `CreateWindowBackend` (`!MAHO_HEADLESS && (_WIN32 || __linux__)` -> `FGlfwWindow`) / `CreateHeadlessBackend` (`__linux__` -> `FEGLHeadlessWindow`). Non-target platforms return an empty backend - callers must not assume a surface always exists, `GetNativeWindow()` may be `nullptr`.
  - `Initialize` is an empty startup (backend is lazily created); `DestroyWindow` can switch to headless; `PollEvents` explicitly pumps events (the engine has no implicit stage loop).
  - Headless build switch: with `-DMAHO_HEADLESS=ON` GLFW is not fetched (`Platform.cmake`), headers/cpp branch by macro; EGL (Linux) is available in both modes.
  - backend details (`FGlfwWindow` / `FEGLHeadlessWindow` / `FPlatformBackend`) are all in the cpp anonymous namespace, the header only exposes the `IPlatform` abstraction.
  - Win32 macro pollution: `Windows.h` (pulled in via GLFW) does `#define CreateWindow`, both the header and cpp top have `#undef CreateWindow` guards, do not delete them.
- Follow the root [AGENTS.md](../../../../AGENTS.md).
