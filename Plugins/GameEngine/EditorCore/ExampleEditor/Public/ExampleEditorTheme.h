#pragma once

#include "ExampleEditorApi.h"

namespace Maho
{

// ── Editor theme data model (imgui-free) ───────────────────────────────
// The editor theme is two flat, named lists:
//   Colors — a color per exposed slot, four floats (r,g,b,a in 0..1).
//   Styles — a numeric per exposed ImGui style field, two floats (v0,v1),
//            where scalar fields use only v0.
// Flat buffers interleave by element:
//   colors[i*4+0..3] = r,g,b,a
//   styles[i*2+0..1] = v0,v1
// The index order is stable for a given engine build (see ImGuiTheme.cpp).

// ── Colors ────────────────────────────────────────────────────────────

/** Number of editable theme colors. */
MAHO_EXAMPLEEDITOR_API int GetEditorThemeColorCount();

/** Display name of the color at `index` (e.g. "FrameBg"). Never null. */
MAHO_EXAMPLEEDITOR_API const char* GetEditorThemeColorName(int index);

/** Display group of the color at `index` (e.g. "Input frames"). Never null. */
MAHO_EXAMPLEEDITOR_API const char* GetEditorThemeColorGroup(int index);

/** Built-in default (r,g,b,a) for the color at `index`, written into out[4]. */
MAHO_EXAMPLEEDITOR_API bool GetEditorThemeColorDefault(int index, float out[4]);

/** Apply a flat color buffer (count*4 floats) to the live ImGui style. */
MAHO_EXAMPLEEDITOR_API void ApplyEditorTheme(const float* flatColors);

/** Re-apply the built-in color defaults to the live style (colors only). */
MAHO_EXAMPLEEDITOR_API void EditorThemeReset();

// ── Styles (rounding / padding / spacing ...) ───────────────────────────

/** Number of editable numeric style fields. */
MAHO_EXAMPLEEDITOR_API int GetEditorThemeStyleCount();

/** Display name of the style at `index` (e.g. "WindowRounding"). */
MAHO_EXAMPLEEDITOR_API const char* GetEditorThemeStyleName(int index);

/** Display group of the style at `index` (e.g. "Tabs"). */
MAHO_EXAMPLEEDITOR_API const char* GetEditorThemeStyleGroup(int index);

/** Number of floats for the style at `index` (1 = scalar, 2 = vec2). */
MAHO_EXAMPLEEDITOR_API int GetEditorThemeStyleArity(int index);

/** Built-in default (v0[,v1]) for the style at `index`, written into out[2]. */
MAHO_EXAMPLEEDITOR_API bool GetEditorThemeStyleDefault(int index, float out[2]);

/** Apply a flat style buffer (count*2 floats) to the live ImGui style. */
MAHO_EXAMPLEEDITOR_API void ApplyEditorThemeStyles(const float* flatStyles);

/** Re-apply the built-in style defaults to the live style (styles only). */
MAHO_EXAMPLEEDITOR_API void EditorThemeStylesReset();

// ── Persistence (whole theme: colors + styles in one file) ─────────────

/** Save colors+styles to `path`. true on success (creates parent dirs). */
MAHO_EXAMPLEEDITOR_API bool SaveEditorTheme(const char* path, const float* flatColors, const float* flatStyles);

/** Load colors+styles from `path` into the provided buffers. Missing sections
 *  are left untouched (pre-fill with defaults before calling). true on success. */
MAHO_EXAMPLEEDITOR_API bool LoadEditorTheme(const char* path, float* outColors, float* outStyles);

/** The persistent path the editor auto-restores on startup (relative to CWD). */
MAHO_EXAMPLEEDITOR_API const char* GetEditorThemeDefaultPath();

} // namespace Maho
