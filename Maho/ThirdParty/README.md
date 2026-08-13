# ThirdParty

Small in-repo assets and optional vendored single-headers. Large C++ libraries are pulled by CMake `FetchContent` (see `Build/CMake/MahoDependencies.cmake`) — do **not** commit their full source trees here.

## Fetched at configure time (not in git)

| Dependency | Source | Pin |
|------------|--------|-----|
| spdlog | FetchContent | v1.15.3 |
| GLFW | FetchContent | 3.4 |
| Dear ImGui | FetchContent | v1.91.9-docking |
| ImGuizmo | FetchContent | master (`src/` layout) |
| imgui-node-editor | FetchContent | develop |
| ImPlot | FetchContent | v0.16 |
| ImGuiFileDialog | FetchContent | v0.6.7 |
| IconFontCppHeaders | FetchContent | main |
| Lua / sol2 / refl-cpp | FetchContent (or optional local override) | see CMake |
| GLM | FetchContent (or optional local override) | 1.0.1 |

First configure needs network. CMake caches downloads under the build tree (`_deps/`).

## Kept in this folder

| Tree | Notes |
|------|--------|
| `fonts/` | UI + icon TTFs copied to `Engine/Fonts` at build (`Inter`, `Roboto`, `fa-solid-900`). |
| `nlohmann/json.hpp` | Optional single-header vendor; else FetchContent nlohmann/json v3.11.3. |
| `lua/`, `sol2/`, `refl-cpp/` | Optional local overrides; else FetchContent. |
