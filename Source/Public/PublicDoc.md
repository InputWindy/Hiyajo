# Public

## Code Files

- [EntryPoint.h](EntryPoint.h) — 统一应用驱动：加载引擎 DLL → `CreateEngine()` → 生命周期
- [EntryPointWindows.h](EntryPointWindows.h) — Windows 双入口（WinMain + main）
- [EntryPointAndroid.h](EntryPointAndroid.h) — Android `android_main`（glue 线程）
- [EntryPointIOS.h](EntryPointIOS.h) — iOS main
- [EntryPointLinux.h](EntryPointLinux.h) — Linux main
- [EntryPointXbox.h](EntryPointXbox.h) — Xbox main
- [Maho.h](Maho.h) — 引擎聚合头：`<Core/Core.h>` + `<Engine/Engine.h>`

## Sub Layers

- [Core](Core/CoreDoc.md) — 类型无关基础设施
- [Engine](Engine/EngineDoc.md) — 层系统

## Concept -- Entry and Aggregation

进程从平台入口（Windows/Android/iOS/Linux/Xbox）收敛到 `EntryPoint.h` 的 `Maho::Main(Argc, Argv)`：它经 `FAssembly` 加载引擎 DLL（默认 `Engine.dll`，可用 `MAHO_ENGINE_NAME` 覆盖），查 `CreateEngine` 导出符号拿到 `FEngineBase*`，按 ParseCommandLine → PreMain → Main → PostMain → delete 的对称生命周期驱动。入口插件是唯一宿主。

`Maho.h` 聚合头一次性引入 Core 基础设施 + Engine 层系统；单个模块可只 include 所需头。

## Related Docs

- [README.md](../../README.md) — 引擎总览
- [SourceDoc.md](../SourceDoc.md) — 源码根
