#pragma once

namespace Maho
{

/**
 * Applies the editor's default night chrome (menubar -> tab strip -> panel) with
 * black dock-chassis gutters. Called once after the editor's OWN ImGui context is
 * created; the style is context-scoped, so every editor window/component inherits it.
 */
void ApplyMahoNightTheme();

} // namespace Maho
