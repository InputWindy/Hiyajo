# Engine（Private）— 实现算法字典

cpp 侧每个函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。

## Engine.cpp

<a id="fn-engine-main"></a>
### FEngineBase::Main()

← [公开 API](../../Public/Engine/EngineAPI.md) · `int`

主循环＝**纯调度 Tick 环**（不建 Init/Shutdown 图：装载与初始化归 `PreMain`，卸载与 Shutdown 归 `PostMain`）。帧之间会重叠——`SubmitFrame` 只等自己要复用的环槽，跨帧安全由图的每层 gate 保证。

```text
Main():
1. 构造 Tick 图（缓存）并绑定 OnLayersChanged → bLayersDirty
2. Tick 循环：
   while true:
     if 有挂起安装/卸载/重载:                       // 拓扑变更要求图静止
       EngineGraph.WaitAll()
       FlushPendingUpdatePipelines<TInit, TShutdown>()
     if bLayersDirty:
       EngineGraph.WaitAll(); EngineGraph.Init(Select<IBeginFrame,ITick,IEndFrame,IExit>())
       Compile 失败 → 报一次 + 退化成空图
     EngineGraph.SubmitFrame()                    // 只等自己要复用的环槽；最多 MAHO_FRAMES_IN_FLIGHT 帧在飞
     if ShouldExit():                             // 本帧已提交完，安全读退出标志
         break
   EngineGraph.WaitAll(); Pool.Flush()            // 退出前：图 + 池 双静止
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

置收集器的"收摊"标志；主循环在本帧提交完成后读到并退出。同一个标志也让
`FLayerCollector::Install` / `Reload` 拒绝（收摊后装载的模块永远等不到它的阶段跑）。

```text
RequestExit():
1. bClosing.store(true, memory_order_release)    // FLayerCollector::bClosing
2. 宿主侧读它用 ShouldExit()；收集器侧读它用 IsClosing()
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

析构默认实现；`GetDependencies()` 返回内部 `Dependencies` 表（引用，不拷贝）。`GetDependents()` 是头里的内联实现，不在此处。

```text
~FLayerBase() = default
GetDependencies(): return Dependencies
```

<a id="fn-layer-adddep-runtime"></a>
### FLayerBase::WaitFor(type_index, string_view, type_index) / BlockOn(string_view, type_index, type_index)

← [公开 API](../../Public/Engine/EngineAPI.md) · `void`（两成员均 `private`，只由 DSL builder 触达）

**按名字寻址**的依赖落点：跨 DLL 的 feature 用层名点名依赖，从而不对可选插件建立构建依赖。头文件里只有声明，实现落在这里；唯一调用者是 `Layer.h` 的 `FWaitForNamedBuilder` / `FBlockOnNamedBuilder` 嵌套类（嵌套类可访问外层 `private`）。点类型的 `WaitFor<...>()` / `BlockOn<...>()` 是模板，内联在头里。

```text
WaitFor(MyStage, OtherName, OtherStage):
1. Dependencies[MyStage].push_back({ string(OtherName), OtherStage })

BlockOn(OtherName, OtherStage, MyStage):
1. Dependents.push_back({ string(OtherName), OtherStage, MyStage })
```

- [EngineDoc.md](EngineDoc.md) — 实现目录 · [公开 API](../../Public/Engine/EngineAPI.md) — 签名入口
