# {{PROJECT_NAME}} plugins

Game-specific plugins go here (`*.cplugin` + `Source/<Name>/`).
Engine plugins live in `Maho/Plugins/` and are enabled via the `.cproject` `Plugins` list.

The game `App` (see `Source/GameApp.cpp`) registers each enabled plugin's extension
in `PreInitialize`; the engine template (`FGameClientEngine` / `FGameServerEngine`)
registers its own world + render extensions.

Plugin Public headers reach the game include path via `Maho::Modules`.
See engine `Maho/Plugins/README.md` for the `.cplugin` schema (`Extension` metadata).
