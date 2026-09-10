# ExampleEditor — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 职责

编辑器宿主，两副身份合一：

- **渲染特性**（`FRender` 的 `IOnInstalled / IEditorInput / IEditorCompose / IPreUnInstall`）：自持**编辑器自己的** ImGui 上下文、`EditorRT` 合成目标、字体上传与最终 present。它是编辑器侧唯一碰 RHI 的地方。
- **子 collector**（`FLayerCollector<FExampleEditor>`）：安装编辑器组件插件（`EditorViewport` / `EditorConsole` / `ContentBrowser` / `EditorTheme`），驱动它们的 `IEditorInit` / `IEditorPanel::Update` / `IEditorShutdown` 图。

帧序：`Select<IEditorPanel>() -> Update`（声明期，`NewFrame` 之前）→ 宿主开 dock 主窗 → 通用视图循环（按编辑器上下文筛选注册表视图、按外壳声明 `Begin/End`、`TranslateView`）→ `Render`。

## 设计约束（强约束）

- **面板只声明树，不画**：`IEditorPanel::Update` 里只改自己的 `UI::FUIView`（`View->Edit()` 独占写 + `View->DrainEvents()`）；面板 DLL 不链接、不包含 ImGui（见根 [AGENTS.md](../../../../AGENTS.md) 第 4 条）。
- **停靠身份归宿主**：宿主开窗前统一 `SetNextWindowDockID(自己的 dockspace id, ImGuiCond_FirstUseEver)`，面板侧不出现 dock id。
- **字体图集归上下文**：上下文创建后立刻 `UI::BakeUIThemeFonts()`（"主题字体 × 档位"一次性全烘，须早于首次取图集数据），销毁上下文前 `UI::ClearUIFonts(上下文)`；字体条目按上下文记账，绝不与游戏侧 `UIFeature` 的同名条目互串。
- **注销顺序**：面板在 `IEditorShutdown` 里 `UnregisterView`，注册表只持裸指针，必须先注销后析构。这条边由 `FRender`（真正驱动卸载的 TopLevel 层）以**名字形** `BlockOn("FUIViewRegistry", IShutdown, IShutdown)` 声明 —— 面板是子插件，自己声明的边会被 collector 子图静默跳过。
- **依赖只走 `.cplugin`**：`Dependencies` 走链接 + 公开 include；跨特性类型引用放 `PrivateIncludes`（编译期 include，不链接）。include 用 `<Name.h>`，不跨目录相对 include。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)（导出规范 / 依赖边 / 关机自洽）。

## 文档

- [ExampleEditor.md](ExampleEditor.md) — 概念与帧序
- [API.md](API.md) — API
