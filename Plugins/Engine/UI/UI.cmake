# UI engine plugin: fetches Dear ImGui (docking branch) and compiles the core
# into this DLL. NO imgui_impl_* backend -- rendering is a custom FRHI backend in
# the project's UIFeature (only the CPU side lives here).
maho_git_repository_url(_IMGUI_URL https://github.com/ocornut/imgui.git)
maho_fetchcontent_populate_or_reuse(imgui ${_IMGUI_URL} v1.91.9-docking imgui.h)
unset(_IMGUI_URL)

target_sources(UI PRIVATE
	"${imgui_SOURCE_DIR}/imgui.cpp"
	"${imgui_SOURCE_DIR}/imgui_demo.cpp"
	"${imgui_SOURCE_DIR}/imgui_draw.cpp"
	"${imgui_SOURCE_DIR}/imgui_tables.cpp"
	"${imgui_SOURCE_DIR}/imgui_widgets.cpp"
)
target_include_directories(UI PUBLIC
	"${imgui_SOURCE_DIR}"
)
