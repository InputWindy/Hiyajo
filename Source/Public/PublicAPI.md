# Public — API

## 代码文件

- [EntryPoint.h](EntryPoint.h) — 统一应用驱动（`Maho::Main`）
- [EntryPointWindows.h](EntryPointWindows.h) — Windows WinMain/main
- [EntryPointAndroid.h](EntryPointAndroid.h) — Android `android_main`
- [EntryPointIOS.h](EntryPointIOS.h) — iOS main
- [EntryPointLinux.h](EntryPointLinux.h) — Linux main
- [EntryPointXbox.h](EntryPointXbox.h) — Xbox main
- [Maho.h](Maho.h) — 引擎聚合头（Core + Engine）

## 子层级

- [Core](Core/CoreAPI.md) — 编译期 + 并发基础设施
- [Engine](Engine/EngineAPI.md) — 层系统
