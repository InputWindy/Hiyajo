# Platform plugin: self-contained output dirs.
# GLFW stays central in MahoDependencies.cmake and is linked by maho_add_plugin_modules
# (Render's imgui glfw backend and ImGuiSystem use it).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")
