# EditorViewport — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- Editor viewport panel -- publishes the scene mirror as a declarative UI tree node (`UI::FUIImage`) and the panel rect the game UI is presented into.
- **面板只声明树，不画**：`IEditorPanel::Update` 里只改自己的 `UI::FUIView`（`Edit()` 独占写 + `DrainEvents()`）；DLL 不链接、不包含 ImGui。视图在 `IEditorShutdown` 里先 `UnregisterView` 再析构；停靠 id 由宿主（`FExampleEditor`）统一施加。
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
