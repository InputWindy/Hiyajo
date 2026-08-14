#pragma once

// Dear ImGui build config for Maho (selected via IMGUI_USER_CONFIG).
#include <Core/Misc/Export.h>

#define IMGUI_API MAHO_API
#define USE_IMGUI_API
#define IMPLOT_API MAHO_API
#define IGFD_API MAHO_API
#define IMGUI_NODE_EDITOR_API MAHO_API

// Required by imgui-node-editor / several ImGui math helpers.
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#	define IMGUI_DEFINE_MATH_OPERATORS
#endif
