# Resource plugin: self-contained.
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the Resource target. No plugin-exclusive
# third-party deps — nlohmann/json is shared with the engine core, FPaths
# (paths) + FName (catalog keys) come in via .cplugin Dependencies.
