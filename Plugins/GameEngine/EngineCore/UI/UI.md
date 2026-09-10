# UI

声明式 UI 组件树：持久状态树（唯一数据源）+ 每帧全量翻译到 ImGui + 跨 DLL 视图注册表。
编辑器与游戏共用同一套组件树；渲染后端由翻译器承担（v1 = ImGui）。

## Code files
- [Public/UI.h](Public/UI.h) — 总览头（依赖方 include 这一个）
- [Public/UIApi.h](Public/UIApi.h) — `MAHO_UI_API` 导出宏（`MAHO_UI_MODULE_EXPORTS` 切换）
- [Public/UITypes.h](Public/UITypes.h) — `FUIName` / 几何 / 五态 / 命中结果
- [Public/UIViewRegistry.h](Public/UIViewRegistry.h) — 层 + 跨 DLL 视图注册表 + `GetUIViewRegistry()`
- [Private/UI.cpp](Private/UI.cpp) — `CreateLayer()` 动态装载入口
- [Private/UIViewRegistry.cpp](Private/UIViewRegistry.cpp) — 注册表实现（发布/撤发布）

## 数据流（目标形态）

```
所有者线程：  View.Edit()  ->  改树（唯一写者）
翻译线程：    SnapshotViews() -> 布局 -> 逐节点读 -> ImGui 绘制 -> 回写运行期状态
宿主：        SetNextWindowDockID（停靠身份归宿主）-> ImGui::Begin -> 翻译器画内容
```

## Related docs
- [API.md](API.md) - API documentation
- [AGENTS.md](AGENTS.md) - 硬约束（公开头不含 ImGui / 线程契约 / 模块名规则）
- `openspec/changes/add-declarative-ui-tree/` - 设计草案（api-draft.md = 逐节代码草案）
