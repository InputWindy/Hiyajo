# Engine（Private）— 实现算法字典

cpp 侧每个函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。

## Engine.cpp

<a id="fn-engine-main"></a>
### FEngineBase::Main()

← [公开 API](../../Public/Engine/EngineAPI.md) · `int`

主循环：Init 图（一次）→ Tick 图（循环）→ Shutdown 图（一次）。每帧先等上一帧排空，再应用挂起安装/卸载，重建 Tick 图后异步派发；检查退出标志。

```text
Main():
1. 把 PendingAdded 合并进 Pipelines（此处不调 FlushPendingUpdatePipelines，避免重复 init）
2. Init 图：
   InitGraph(FInitStages=IPreInit,IInit,IPostInit).Init(Select<这些阶段>())
   Compile 失败 → ReportFatal
   Execute(); Flush()
3. Tick 循环：
   while true:
     EngineGraph.Flush()                          // 等上一帧排空
     FlushPendingUpdatePipelines<IPreInit,IInit,IPostInit>()   // 应用挂起安装/卸载
     EngineGraph.Init(Select<IBeginFrame,ITick,IEndFrame,IExit>())
     Compile 失败 → ReportFatal
     EngineGraph.Execute()                        // 异步派发
     if bIsShuttingDown:                          // 本帧 Execute 已派发完，安全读退出标志
         EngineGraph.Flush(); break
4. Shutdown 图：ShutdownGraph(IPreShutdown,IShutdown,IPostShutdown) 同 Init 图跑一次
5. Features.clear(); Modules.clear()             // 先删实例（虚析构在各自 DLL），再卸 DLL
6. return 0
```

<a id="fn-engine-parsecmd"></a>
### FEngineBase::ParseCommandLine(int Argc, char** Argv)

← [公开 API](../../Public/Engine/EngineAPI.md) · `void`

把 `-key` / `-key=value` / `-key value` / 裸 flag 归一化成 CLI11 长选项，按 key 声明选项并解析，读回 KV store。

```text
ParseCommandLine(Argc, Argv):
1. Normalized = ["maho"]                        // CLI11 要程序名槽位
2. for Arg in Argv[1..]:
     if 非 "-" 开头: continue                   // 位置参数忽略
     if "--" 开头: 原样保留
     elif 含 '=': 转 "--key=value"
     elif 下一个参数非 "-" 开头: "--key=next" 并跳过 next
     else: "--key=true"                         // 裸 flag → true
3. 收集全部唯一 key，为每个声明 App.add_option("--key")->expected(1)
4. App.parse(Argc, Argv)                        // CLI11 真正分词；ParseError 非致命，继续
5. 逐 key 把解析结果读回 Store
```

<a id="fn-engine-requestexit"></a>
### FEngineBase::RequestExit()

← [公开 API](../../Public/Engine/EngineAPI.md) · `void`

置原子退出标志；主循环在本帧 Execute 派发完成后读到并退出。

```text
RequestExit():
1. bIsShuttingDown.store(true, memory_order_release)
```

<a id="fn-engine-kv"></a>
### Has / Get / GetBool / GetInt / GetAll

← [公开 API](../../Public/Engine/EngineAPI.md)

命令行 KV 读取器：`Has` 查 key 存在；`Get` 取值（缺省空串）；`GetBool` 判定 "true/1/yes/on"；`GetInt` `stoi`（失败回 0）；`GetAll` 返回整个 map。

```text
Get(Key): return Store.find(Key) != end ? value : ""
GetBool(Key): Get(Key) in {"true","1","yes","on"}
GetInt(Key): 空 → 0; stoi(Get(Key)) 失败 → 0
```

## Layer.cpp

<a id="fn-layer-dtor"></a>
### FLayerBase::~FLayerBase() / GetDependencies()

← [公开 API](../../Public/Engine/EngineAPI.md) · `virtual` / `const FDependencyTable&`

析构默认实现；`GetDependencies()` 返回内部 `Dependencies` 表（引用，不拷贝）。

```text
~FLayerBase() = default
GetDependencies(): return Dependencies
```

<a id="fn-layer-adddep-runtime"></a>
### FLayerBase::AddDependency(type_index, string_view, type_index)

← [公开 API](../../Public/Engine/EngineAPI.md) · `void`

运行时字符串寻址的依赖声明（跨 DLL feature 用层名点名依赖）：`this 在 MyStage 依赖 DepName 在 DepStage`。

```text
AddDependency(MyStage, DepName, DepStage):
1. Dependencies[MyStage].push_back({ string(DepName), DepStage })
```

- [EngineDoc.md](EngineDoc.md) — 实现目录 · [公开 API](../../Public/Engine/EngineAPI.md) — 签名入口
