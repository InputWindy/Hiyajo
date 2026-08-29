# Platform

## Code Files

- [Platform.h](Platform.h) - native surface service (`FNativeSurface` / `IPlatform` / `FPlatformSystem`)

## Concept - Native Surface + Events

Native surface singleton service - provides **native window/context handles** to RHI: `FNativeSurface` is a `void*` opaque pointer (`GLFWwindow*` / `EGLContext` / `ANativeWindow*` / `UIView*` ...). Desktop (Win32/Linux) uses a **GLFW window**; Linux can use an **EGL pbuffer headless context** (no OS window). Window semantics are hidden behind the single `IPlatform::GetNativeWindow()` accessor - windowless platforms (headless / Android surface / iOS view) do not need to expose the window concept.

### FPlatformSystem - surface service (singleton)

`TSingleton<FPlatformSystem>` + `IPlugin<IInit, IShutdown>`:

- **Initialize(Argc, Argv)**: empty startup - backend is **lazily created** by `CreateWindow` / `CreateHeadlessContext`.
- **CreateWindow(W, H, Title)**: selects the backend for the current platform (desktop GLFW window), returns true on success (Surface and native handle non-null).
- **CreateHeadlessContext(W, H)**: EGL pbuffer headless context on Linux (OpenGL ES 2.0).
- **DestroyWindow**: destroys the backend (reset surface + event closure) - can switch to headless.
- **PollEvents**: explicitly pumps platform events (once per frame) - the engine has no implicit stage loop; no-op when there is no backend.
- **GetNativeWindow**: native handle; `nullptr` when headless or creation failed.
- **IsHeadless**: `Surface == nullptr`.
- **ShouldClose**: window close request (`glfwWindowShouldClose`); always false when headless or no events.

Host-driven fixed lifecycle:

```cpp
Platform::FPlatformSystem::Get().Initialize(0, nullptr);
Platform::FPlatformSystem::Get().CreateWindow(1280, 720, "MyGame");
while (!Platform::FPlatformSystem::Get().ShouldClose())
{
    Platform::FPlatformSystem::Get().PollEvents();
    // render frame...
}
Platform::FPlatformSystem::Get().Shutdown();
```

## Third-Party Dependencies

- **GLFW 3.4** (`Platform.cmake` FetchContent, desktop window backend; not fetched when `-DMAHO_HEADLESS=ON`).
- **EGL** (Linux, headless backend, available in both modes).
- Other plugins: none - `.cplugin` Dependencies = `[]` (engine core is auto-linked via codegen).

## Related Docs

- [API.html](API.html) - API documentation (public signatures)
- [ImplAPI.html](ImplAPI.html) - implementation algorithm dictionary (cpp function pseudocode)
