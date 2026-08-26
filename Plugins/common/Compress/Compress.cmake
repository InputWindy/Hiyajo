# Compress plugin: third-party dependencies.
# zstd is an ENGINE-level dep — fetched once in Build/CMake/MahoDependencies.cmake
# and linked PUBLIC into Maho (libzstd_static), so this plugin gets <zstd.h> +
# the static lib transitively via Maho. No fetch here; the DLL target is built by
# codegen.
