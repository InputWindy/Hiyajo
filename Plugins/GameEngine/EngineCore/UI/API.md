# UI — API 文档

声明式 UI 组件树：持久状态树（唯一数据源）+ 每帧全量翻译到 ImGui + 跨 DLL 视图注册表。

公开头清单：`UI.h`（总览）、`UIApi.h`（导出宏）、`UITypes.h`、`UIViewRegistry.h`。
**公开头不含 `<imgui.h>`** —— 翻译器只在 `Private/` 里。

## UITypes.h — 基础类型

- `FUIName` = `Name::FName`（组件 Id；同级唯一，跨级可重名，路由走路径）
- `FUIVector2` / `FUIRect`（`Min/Max/Contains/Intersect/IsEmpty`）/ `FMargin`（0/1/2/4 参数构造）
- `EUIState`：`Normal/Hovered/Pressed/Selected/Disabled/Count` + `kUIStateCount`
- `FUIHitResult`：翻译阶段写回节点的交互意图（`bHovered/bPressed/bClicked/bDragging/bReleased`）

## UIViewRegistry.h — 跨 DLL 视图注册表

```cpp
MAHO_UI_API FUIViewRegistry* GetUIViewRegistry();   // 与 GetLog() 同形；未安装/已关闭 = nullptr

class FUIViewRegistry : public FLayer<IInit, IShutdown>
{
public:
	MAHO_DECLARE_LAYER(FUIViewRegistry);

	void RegisterView(FUIView& View);          // 宿主线程；表只存裸指针，不拥有
	void UnregisterView(FUIView& View);        // 析构前必须调用
	[[nodiscard]] std::vector<FUIView*> SnapshotViews() const;   // 锁内快照，遍历不持锁

private:
	void Initialize(FEngineBase& Engine) override;   // GUIRegistry = this
	void Shutdown(FEngineBase& Engine) override;     // 清空 + GUIRegistry = nullptr
};
```

- 宿主 Install 一次（`Example/ExampleEngine/ExampleEngine.cproject` 的 `Plugins` 里挂 `UI`，运行时由 `PluginCatalog.json` 的 TopLevel 装）。
- `Initialize` 里发日志，故声明了 `MyStage<IInit>().IsWaiting<FLog>().ForStage<IInit>()`。
- `Shutdown` 时仍在册的视图会打一条 Warn —— 那是所有权漏了，不是正常路径。

## 尚未落地（按 tasks.md 推进）

`UIStyle.h` / `UILayout.h` / `UIEvent.h` / `UIResource.h` / `UITheme.h` / `UITranslate.h` / `FUIBuilder.h` / `UIView.h` / `Widgets/*`（15 个组件头）。
