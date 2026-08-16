# TempRebuildCode — Maho 引擎重构临时工区

所有重构后的引擎类和接口实现放在这里（头文件名也会重新设计）。全部完成后，手动整体替换 `Maho/` 源代码。

## 目录结构

```text
TempRebuildCode/
├── Maho/                          # 重构后的引擎
│   ├── CMakeLists.txt              # 引擎 DLL 入口（已拷）
│   ├── Maho.cmake                  # 核心三方库 FetchContent（已拷）
│   ├── Source/
│   │   ├── Public/Core/{Engine, Misc, Server}/   # 接口 + 数据结构（待重构）
│   │   └── Private/Core/{Engine, Misc, Server}/  # 实现（待重构）
│   └── Plugins/                    # 插件（.cmake/.cplugin 框架已拷，Source 待重构）
│       ├── Platform/
│       ├── Render/                 # RDG / RHI / Shader / UI
│       ├── Resource/
│       ├── Script/
│       └── World/                  # ECS
├── Build/                          # CMake 框架（已拷：CMake 模块 + workspace + Templates）
├── Tools/                          # Python 工具框架（已拷）
└── README.md                       # 本文件
```

## 已拷贝（框架，不依赖头文件名）

- `Build/`：`CMakeLists.txt` + `CMakePresets.json` + `CMake/{MahoDirectories, MahoHelpers, MahoDependencies}.cmake` + `Templates/`
- `Tools/`：`maho_tools.py` / `create_project.py` / `generateProject.py` / `package.py` / `scan_plugins.py` 等 + 薄 `.bat`/`.vbs` 启动器
- `Maho/CMakeLists.txt` + `Maho/Maho.cmake`
- `Maho/Plugins/*/{*.cmake, *.cplugin, .gitignore}`

## 未拷贝（重构时重新设计）

- 所有 `.h` / `.cpp`（头文件名重新设计）
- 所有 README / CONTRACT
- `Maho/Shaders/` / `Maho/Content/`（内容，非框架）

## 编码规范

遵循根目录 [AGENTS.md](../AGENTS.md)：F/I/E/T/b 前缀、Allman 括号、Tab 缩进、英文注释、Public 只放接口、聚合头与模块同名、模板元编程优先、依赖声明用 `Core/Topology.h`（编译期拓扑排序 + 环检测）。
