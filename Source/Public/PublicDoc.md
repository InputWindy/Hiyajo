<!-- mahogen -->
# Public

## Code Files

- [EntryPoint.h](EntryPoint.h)
- [EntryPointAndroid.h](EntryPointAndroid.h)
- [EntryPointIOS.h](EntryPointIOS.h)
- [EntryPointLinux.h](EntryPointLinux.h)
- [EntryPointWindows.h](EntryPointWindows.h)
- [EntryPointXbox.h](EntryPointXbox.h)
- [Maho.h](Maho.h)

## Sub Layers

- [Core](Core/CoreDoc.md)
- [Engine](Engine/EngineDoc.md)
<!-- mahogen end -->

## Concept -- Entry and Aggregation

- [Maho.h](Maho.h) -- engine aggregate header (unified include of core public headers)
- [EntryPoint.h](EntryPoint.h) -- generic entry driver: `FAssembly` loads the project root plugin DLL -> `CreateEngine()` -> drives `FEngineBase`
- `EntryPoint{Windows,Linux,Android,IOS,Xbox}.h` -- per-platform entry forwarding

## Related Docs

- [Core/CoreDoc.md](Core/CoreDoc.md) -- core infrastructure (type-agnostic building blocks)
- [Engine/EngineDoc.md](Engine/EngineDoc.md) -- layer architecture (FLayerBase/FLayer/FLayerTaskGraph/main loop)
- [../SourceDoc.md](../SourceDoc.md) -- source root
