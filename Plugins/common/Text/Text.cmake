# Text plugin: third-party dependencies.
# nlohmann/json (header-only) is an ENGINE-level dep — fetched once in
# Build/CMake/MahoDependencies.cmake and linked PUBLIC into Maho
# (nlohmann_json::nlohmann_json), so this plugin gets <nlohmann/json.hpp>
# transitively via Maho. No fetch here; the DLL target is built by codegen.
