# ExampleEngine — API 文档

入口插件（entry template）：应用根，唯一宿主。继承 `FEngineBase`，导出 `CreateEngine()`。

## FExampleEngine <class : FEngineBase>

应用根。`PreMain` 从安装树装载根的直系子层并驱动 Init 阶段（`FPluginManager::Get().Load()` → `InstallChildrenOf(GetName())` → `FlushPendingUpdatePipelines<Init, Shutdown>()`）；`PostMain` 是出口钩子（关闸门 → 卸全部活层 → 排干）。

#### 接口

| 签名 | 说明 |
|------|------|
| `void PreMain() override` | 装载 + 初始化：安装树 → `InstallChildrenOf(GetName())` → `FlushPendingUpdatePipelines<IPreInit,IInit,IPostInit, IPreShutdown,IShutdown,IPostShutdown>()` |
| `void PostMain() override` | 出口钩子（本例先调 `FEngineBase::PostMain()`） |

## MAHO_DECLARE_ENGINE(FExampleEngine) <宏>

生成 `static FEngineBase* CreateEngine()` 工厂 + `GetModulePath()`。

## CreateEngine <导出函数>

`extern "C"` bridge，EntryPoint 按符号名 `"CreateEngine"` 查找：

```cpp
extern "C" MAHO_EXAMPLEENGINE_API Maho::FEngineBase* CreateEngine()
{
    return Maho::FExampleEngine::CreateEngine();
}
```

- [ExampleEngine.md](ExampleEngine.md) — 概念 · [README](../../README.md) — 项目走读
