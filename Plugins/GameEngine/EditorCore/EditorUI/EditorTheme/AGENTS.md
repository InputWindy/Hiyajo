# EditorTheme — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- Editor theme configurator panel - expose every theme color, live apply + save/load.
- **面板只声明树，不画**：`IEditorPanel::Update` 里只改自己的 `UI::FUIView`；主题落到后端 ImGui 自带配色走 UI 插件的 `UI::ApplyThemeToImGuiStyle()`，DLL 不链接、不包含 ImGui。字号档位集合改动需重启生效（图集只在启动烘制）。
- 视图在 `IEditorShutdown` 里先 `UnregisterView` 再析构；停靠 id 由宿主（`FExampleEditor`）统一施加。
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
