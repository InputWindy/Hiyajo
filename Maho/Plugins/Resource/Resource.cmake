# Resource plugin: self-contained output dirs.
# No plugin-exclusive third-party deps (nlohmann/json is shared with Maho core).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")
