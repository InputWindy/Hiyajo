<!-- mahogen -->
# Public

## 代码文件

- [EntryPoint.h](EntryPoint.h)
- [EntryPointAndroid.h](EntryPointAndroid.h)
- [EntryPointIOS.h](EntryPointIOS.h)
- [EntryPointLinux.h](EntryPointLinux.h)
- [EntryPointWindows.h](EntryPointWindows.h)
- [EntryPointXbox.h](EntryPointXbox.h)
- [Maho.h](Maho.h)

## 子层级

- [Core](Core/CoreDoc.md)
- [Engine](Engine/EngineDoc.md)
<!-- mahogen end -->

## 概念——入口与聚合

- [Maho.h](Maho.h) — 引擎聚合头（统一 include 核心 public 头）
- [EntryPoint.h](EntryPoint.h) — 通用入口驱动：`FAssembly` 加载项目根插件 DLL → `CreateEngine()` → 驱动 `FEngineBase`
- `EntryPoint{Windows,Linux,Android,IOS,Xbox}.h` — 各平台入口转发

## 相关文档

- [Core/CoreDoc.md](Core/CoreDoc.md) — 核心基础设施（类型无关积木）
- [Engine/EngineDoc.md](Engine/EngineDoc.md) — 层架构（FLayerBase/FLayer/FLayerTaskGraph/主循环）
- [../SourceDoc.md](../SourceDoc.md) — 源码根
