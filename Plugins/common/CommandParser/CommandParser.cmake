# CommandParser plugin: third-party dependencies.
# CLI11 (header-only) is an ENGINE-level dep — fetched once in
# Build/CMake/MahoDependencies.cmake and linked PUBLIC into Maho
# (CLI11::CLI11), so this plugin gets <CLI/CLI.hpp> transitively via Maho.
# No fetch here; the DLL target is built by codegen.
