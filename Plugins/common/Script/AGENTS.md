# Script — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 定位

多语言脚本宿主（`FScriptSystem`）+ 每语言一个后端（`IScriptLanguage`）。
宿主只做语言注册/调度/生命周期；VM 细节、类型绑定、脚本路径全在各自后端里。

## 架构（强约束）

```
FScriptSystem（宿主，`FScriptSystem::Get()` 进程唯一）
  └─ IScriptLanguage（抽象后端接口，`Script.h`）
       ├─ FLuaLanguage    — Lua 后端（sol2，当前 default）
       ├─ FPythonLanguage — Python 后端（CPython 嵌入 + Scapix 桥，TestGame 工程内）
       └─ FScriptCSharp   — C# 后端（规划中：Mono + Scapix）
```

- **宿主零语言知识**：`RegisterLanguage(IScriptLanguage*)` 按 `GetName()` 幂等注册；`GetActive()` 返回第一个注册的后端；`DoFile/Call/LoadScript<>` 转发到 active 后端。
- **后端必须自己管生命周期**：宿主 `Initialize` 会逐个 `Initialize(Argc, Argv, "Scripts")`；`Shutdown` 对称关闭。
- **语言无关值 = opaque `void*`**：`GetState()`/`CallHandle(void*)`/`FTypeBinder = void(*)(void*)` 全部不透明；只有知道语言类型的调用方在 `include <sol/sol.hpp>` 等之后 cast。
- **Lua 绑定宏**（`MAHO_LUA_BIND_BEGIN/FIELD/METHOD_FN/BIND_END` + `MAHO_LUA_BIND_REGISTER`）只生成 sol2 代码，属于 Lua 后端专属语法糖，勿用于其他语言。
- 宿主不耦合项目逻辑：脚本文件加载、每帧 `OnUpdate` 驱动都是宿主/项目层的事，FScriptSystem 只给执行原语。
- 依赖只走 `.cplugin` `Dependencies`；`Script.h` 必须保持 sol-free（sol2 只出现在 `Private/Script.cpp`）。

## 已知问题 / 待办

### ⚠️ Scapix 生成器不支持中文路径（2026-08-26 验证）

- **现象**：引擎位于 `C:\Users\luchunyi01\Desktop\书架\Hiyajo`（含中文 `书架`）。`scapix.exe`（clang 内核，预编译 `scapix_bin`）解析桥头文件时报 `error: no such file or directory`，中文被当 GBK 字节处理 → 生成失败 → MSBuild 反复重试，表现为"构建卡住"（一堆 cmake/python 进程无 CPU）。
- **验证**：ASCII 路径（`C:\temp\scapix_test`）下 scapix.exe 正常生成 python/cs/java/js/objc 全部桥代码；中文路径必现失败。
- **影响**：`ScriptPython` 桥（`scapix_bridge_headers`）在中文路径下无法构建。当前已在 `TestGame.cproject` 中 `Enabled: false`。
- **候选解法**：
  1. 引擎 + 工程迁到纯 ASCII 路径（如 `C:\Maho`）— 彻底解决。
  2. CMake 里把桥头复制到 ASCII 临时目录生成，产物拷回 — 复杂。
  3. 放弃 Scapix，改用纯 pybind11 手写绑定 — 失去自动生成。
- **恢复条件**：路径 ASCII 化后才能重新 `Enabled: true`。

### Python 后端接入要点（供恢复时参考）

- `ScriptPython.cmake`：cmodule（v2.3.0）→ `find_package(Scapix)` → 两个 target 身份分离：
  - `ScriptPython.dll`（宿主，导出 `CreateLayer` 给引擎，禁 include 桥头）
  - `ScriptPythonBridge.pyd`（Scapix `PYBIND11_MODULE`，`scapix_bridge_headers` 独立 target）
- MSVC Debug 嵌入 CPython：`pyconfig.h` 在 `_DEBUG` 下 pragma 链接 `pythonXXX_d.lib`（官方安装包没有）→ 用 `target_link_options /NODEFAULTLIB:python<M><m>_d.lib` + include Python.h 前临时 `#undef _DEBUG`。
- 宿主 `Initialize` 里 `PyImport_ImportModule("ScriptPythonBridge")` 导入桥模块，桥类落在 `testgame` 命名空间。

## 文档

- 遵循根 [AGENTS.md](../../../../AGENTS.md)
