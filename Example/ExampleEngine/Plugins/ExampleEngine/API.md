# ExampleEngine — API 文档

入口插件（entry template）：应用根，唯一宿主。继承 `FEngineBase`，导出 `CreateEngine()`。

## FExampleEngine <class : FEngineBase>

应用根。`PreMain` 安装引擎服务层（Log/Config/Platform/Resource/Script/Render）；`PostMain` 出口钩子。

#### 接口

| 签名 | 说明 |
|------|------|
| `void PreMain() override` | 安装引擎服务层（`Install("Log.dll")` 等） |
| `void PostMain() override` | 出口钩子（本例为空） |

## MAHO_DECLARE_ENGINE(FExampleEngine, "ExampleEngine.dll") <宏>

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
