# ExampleEngine

## Code files

- [ExampleEngine.h](Public/ExampleEngine.h) — 应用根：`FExampleEngine : FEngineBase`
- [ExampleEngine.cpp](Private/ExampleEngine.cpp) — `PreMain` 安装引擎服务层 + `CreateEngine` bridge
- [ExampleEngine.cplugin](ExampleEngine.cplugin) — 依赖表

## Concept -- Entry Plugin

入口插件是唯一宿主：继承 `FEngineBase`，导出 `CreateEngine()` 供 `EntryPoint` 经 `FAssembly` 查找。它**只调度**，不点名任何具体服务：`PreMain` 从**安装树**（`PluginManager.json`，由 `.cproject` 的 TopLevel 生成）装载**根节点的直系子层**，再驱动 Init 阶段跑完初始化。每个被装的层再把自己的子插件装进**它自己的 collector**（Render 装它的 feature、ExampleEditor 装它的面板、GameWorld 装 UISystem）——所以宿主既不需要写死 DLL 名，也不需要知道谁装谁。

```cpp
void FExampleEngine::PreMain()
{
    FPluginManager::Get().Load();          // 安装树（根节点 = 本插件的层名 FExampleEngine）
    InstallChildrenOf(GetName());          // 装根的直系子层：FConfig / FException / FLog / FNamePool /
                                           // FUIViewRegistry / FPaths / FPlatform / FResourceSystem /
                                           // FScriptSystem / FTextManager / FTimer / FRender / FGameWorld
    FlushPendingUpdatePipelines<           // 驱动 Init 阶段（IPreInit → IInit → IPostInit）
        TTypeList<IPreInit, IInit, IPostInit>,
        TTypeList<IPreShutdown, IShutdown, IPostShutdown>>();
}
```

DLL 名由层类型名 + 平台后缀推出（`FLog → FLog.dll`），`InstallChildrenOf` 负责这一步映射；未挂载的层不会出现在树里，无需 `#if` 守卫。

窗口语义由 `FPlatform` 承担：每帧 `PollEvents`，窗口关闭时请求引擎退出。渲染由 `FRender` 子系统自洽驱动（见 Scene / DrawTriangleFeature）。

## Related docs

- [API.md](API.md) - API documentation
- [README](../../README.md) - 项目走读
