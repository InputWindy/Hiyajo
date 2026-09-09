#pragma once

struct ImVec4;

namespace Maho
{

/**
 * Applies the editor's default night chrome (menubar -> tab strip -> panel) with
 * black dock-chassis gutters. Called once after the editor's OWN ImGui context is
 * created; the style is context-scoped, so every editor window/component inherits it.
 */
void ApplyMahoNightTheme();

/** The theme's background "black" knob (identical to the Bg used for WindowBg). */
const ImVec4& GetEditorBgColor();

} // namespace Maho
