#pragma once

// Optional Dear ImGui extensions (linked into Maho.dll via the imgui static lib).
#include <imgui.h>

#if defined(MAHO_WITH_IMGUI)
#	include <ImGuizmo.h>
#	include <implot.h>
#	include <imgui_node_editor.h>
#	include <ImGuiFileDialog.h>
#	include <IconsFontAwesome6.h>
#endif
