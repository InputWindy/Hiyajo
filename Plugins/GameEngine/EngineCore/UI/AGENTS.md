# UI — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 声明式 UI 组件树：**持久化带状态的组件树**（树是唯一数据源）+ **每帧全量翻译成 ImGui**（翻译器对树只读）。
- **一个模块一个层**：本 DLL 唯一的层是 `FUIViewRegistry`（`MAHO_DECLARE_LAYER`），codegen 由它推导模块名，产物为 `FUIViewRegistry.dll`；`Public/UI.h` 只是总览头，**不得**再放第二个 `MAHO_DECLARE_LAYER`。
- **公开头不得包含 `<imgui.h>`**。ImGui 只允许出现在 `Private/`（翻译器）。这个边界让 UI 的编译依赖不泄漏 ImGui，也让将来换后端只改翻译器。
- **跨 DLL 访问走导出函数**：`GetUIViewRegistry()` 与 `GetLog()` 同形（`Plugins/Common/Log/Public/Log.h:28`），**没有 `static Get()`、没有 `TSingleton`**；层未安装或已关闭时返回 `nullptr`，取用前判空。
- **线程契约**：宿主线程独占写树（`FUIView::Edit()`），翻译线程共享读；跨线程共享的矩形/样式/事件状态由 `std::shared_mutex` 保护。注册表内部自带互斥，只提供"注册/注销 + 锁内快照"，不持有视图所有权。
- **依赖只走 `.cplugin` `Dependencies`**（当前 `Name`、`Log`），include `<Name.h>`，不跨目录相对 include；**不依赖 `Resource`/`Render`**（资源解析走回调注入，见 `UIResource.h`）。
- 视图所有权归各自所有者（编辑器面板 / 游戏侧系统）：注册表从不删除别人注册的视图；所有者先 `UnregisterView` 再析构。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)（导出规范 / 依赖边 / 关机自洽）。

- **注销顺序由驱动卸载的那一层声明**：注册表只持裸指针，视图必须先 `UnregisterView` 再析构。子插件自己声明的 `BlockOn` 会被静默跳过（collector 子图只含自己的待处理集），所以这条边要由真正的 TopLevel 驱动层声明 —— 游戏侧 `FGameWorld`、编辑器侧 `FRender` —— 并且用**名字形**（不强制对可选插件产生构建依赖）。

- **字体图集按上下文记账**：编辑器与游戏各有独立 ImGui 上下文与图集，`RegisterUIFont`/`FindUIFont`/`ClearUIFonts` 的键是 `(上下文, 字体引用, 档位)` —— 同一个 `ImFont*` 只能喂给造它的那个上下文。宿主在 `CreateContext()` 之后、首次取图集之前调一次 `BakeUIThemeFonts()`（"主题字体 × 档位"一次性全烘，运行期不再改图集），并在销毁上下文前 `ClearUIFonts(上下文)`。

## 当前状态

`add-declarative-ui-tree` 已落地：层 + 注册表 + 组件树（`FUIBuilder`）+ 布局引擎 + 样式/主题解析 + 事件（含拖放）+ ImGui 翻译器 + 15 个组件类型；游戏侧 `UISystem` 与四个编辑器面板已全部改为声明组件树，面板 DLL 不再链接/包含 ImGui。

- [UI.md](UI.md) — 概念与文件图
- [API.md](API.md) — API
