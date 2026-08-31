# ExampleEngine

## Code files

- [ExampleEngine.h](Public/ExampleEngine.h) — 应用根：`FExampleEngine : FEngineBase`
- [ExampleEngine.cpp](Private/ExampleEngine.cpp) — `PreMain` 安装引擎服务层 + `CreateEngine` bridge
- [ExampleEngine.cplugin](ExampleEngine.cplugin) — 依赖表

## Concept -- Entry Plugin

入口插件是唯一宿主：继承 `FEngineBase`，导出 `CreateEngine()` 供 `EntryPoint` 经 `FAssembly` 查找。它**只调度**，不实现任何具体服务——引擎服务层（Log/Config/Platform/...）和渲染子系统（Render）都在 `PreMain` 里 `Install("Xxx.dll")` 动态装载。

```cpp
void FExampleEngine::PreMain()
{
    Install("Log.dll");
    Install("Config.dll");
    Install("Platform.dll");
    Install("Resource.dll");
    Install("Script.dll");
    Install("Render.dll");
}
```

窗口语义由 `FPlatform` 承担：每帧 `PollEvents`，窗口关闭时请求引擎退出。渲染由 `FRender` 子系统自洽驱动（见 Scene / DrawTriangleFeature）。

## Related docs

- [API.md](API.md) - API documentation
- [README](../../README.md) - 项目走读
