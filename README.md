# Maho

<div style="background:#143d2b;border:1px solid #3f8f63;border-radius:8px;padding:12px 16px;color:#cdeadd;">

<b>作者寄语：Coding前请务必让AI通读 AGENTS.md 学习规范 —— 因为写代码的是AI，而不是程序员。</b>

</div>

## 简介

Maho 是一个纯 C++20 的游戏引擎，核心设计围绕**并行调度**与**插件式架构**展开。它借鉴 Unity DOTS 的理念：把应用拆解成一组职责单一的功能层，由调度器按 stage 自动编排执行——外层跨层串行、同层并行，层间屏障同步。

一个功能就是一个插件，一个插件就是一个 DLL。引擎核心只提供少量通用积木，所有能力——日志、序列化、物理、渲染——都以插件形式存在，按需安装。

## 特性

- **两类插件**：工具（`TTool`）、层（`TLayer`）——Tool 即插即用（全 public 单例），Layer 是应用根/嵌套宿主（Assembly + 并行调度）。不再区分 Engine。
- **并行遍历基座**：`Execute` 只做并行遍历（编译期类型列表 / 运行时实例数组），无 stage 语义；宿主在 visitor lambda 里决定每个目标干什么（工具单例直调 / 层实例驱动）。
- **编译期组装**：插件组合、品种过滤（`TFilter`）、遍历展开全部在编译期由模板完成，运行时零反射、零排序开销。
- **能力可选**：单例（`TSingleton`）、可运行（`IRunable`）、可安装（`IAssembly`）——插件自己决定要哪些，编译器强制互斥约束。
- **零三方依赖的核心**：引擎核心不含任何第三方库，每个插件在自己的 `.cmake` 里用 FetchContent 拉取依赖，镜像与代理可配置。
- **纯通用核心**：核心不预设 stage 枚举、不预设应用形态，全部下放应用层。

## 构建流程

### 0. 首次自举

```bat
Setup.bat
```

安装引擎本地 Python（`Tools/python` junction → `%LOCALAPPDATA%\Maho\python\tooling`）。优先复用宿主机的 Python（带 tkinter）建 venv，否则下载 python.org installer 静默安装。**全程不依赖系统 PATH 上的 python**。

### 1. 创建项目

```bat
CreateProject.bat
```

打开 UI，填项目名 + 勾选引擎插件（依赖自动解析）。生成：

```
<项目>/
  <项目>.cproject
  Source/                     ← 项目自己的插件（入口 + 子插件）
    <项目>/Public/            ← 入口插件接口
    <项目>/Private/           ← 入口插件实现
    <子插件>/Public/          ← 功能插件
  Extension/                  ← 三方插件目录（与 Source 同级）
  Intermediate/Main.cpp       ← 入口（code-gen，不改）
  CMakeLists.txt / package.bat / CreatePlugin.bat
```

### 2. 生成工程

双击 `<项目>.cproject`（或 `Tools/generateProject.bat <项目>.cproject`）：

- codegen 生成 `Intermediate/Generated/<项目>.gen.h`（依赖插件 forward-declare + 扩展宏）
- 扫描引擎 `Extension/`、项目根插件、项目 `Extension/`，重写 CMakeLists（每个插件一个 DLL target）
- 探测本机 Visual Studio（vswhere）选 CMake 生成器
- 产出 `<项目>.sln`

### 3. 构建

用 Visual Studio 打开 `.sln` 构建，或命令行：

```bat
cmake -S . -B Intermediate -G "Visual Studio 17 2022" -A x64
cmake --build Intermediate --config Debug
```

产物：`Intermediate/Debug/` 下各插件 DLL + `EntryPoint.exe`。构建前自动运行插件依赖环检测（`MahoCheckCycle`）。

### 4. 打包

```bat
package.bat
```

Release 构建并拷贝 exe + 全部 DLL 到 `Packaged/Win64/Release/`。

## 核心概念

### 三类插件

新建插件时按角色选模板：

| 模板 | 角色 | 单例 | 调度器 | 说明 |
|------|------|------|--------|------|
| `TTool<T>` | 工具 | ✅ | ❌ | 即插即用，全 public，谁用谁 `Get().xxx()` |
| `TLayer<Ts...>` | 应用根/嵌套宿主 | ❌ | ✅ 并行 | 可动态安装（Assembly 导出 CreateExtension），并行调度自己 FExtensions 里的工具/子层 |

```cpp
class FLog      : public Maho::TTool<FLog> { ... };                       // 工具，全 public
class FRenderer : public Maho::TLayer<FLog, FRDG> { ... };                // 嵌套层
class FMyGame   : public Maho::TLayer<FLog, FRDG, FRenderer> { ... };     // 应用根
```

### 管理者 vs 拓展

**关键语义**：`TExtension<TExtensions...>` 列表里的插件，跟管理者**既不是平级、也不是继承**——它们是"我要用到的工具 / 我要驱动的子层"。

- **使用**：`TLayer<FLog, FRDG>` 声明"我调度这两个工具"
- **拓展**：`class FMyPlugin : public FBase` 才是真正扩展基类的能力

所以插件按角色放目录：

```
Extension/Tool/   ← 工具（TTool，单例）
Extension/Layer/  ← 层/宿主（TLayer，Assembly）
Extension/Engine/ ← （旧目录，现在也是 TLayer；创建工程的默认起点）
```

### stage 驱动

调度器只提供**并行遍历基座** `Execute`（无 stage 语义）。生命周期由宿主调度：init/tick/shutdown 各调一次 Execute，传个 lambda 决定该阶段每个目标干什么：

```cpp
int Main(int Argc, char** Argv) override
{
    CreateLayers();   // 实例化子 Layer（CreateExtension）进 this->Layers

    Execute<FTools>([](T& Tool) {   // 工具：编译期单例，遍历器把实例传进来
        Maho::Log::FLog::Get().Initialize();   // Tool 即 T& 单例
    });
    Execute(Layers, [](Maho::IAssembly* L) { ... });   // 层：运行时实例
    return 0;
}
```

### 能力可选

引擎不强制任何能力，插件自己决定：

- **单例**（`TSingleton`）：进程内唯一实例，工具和层要
- **可运行**（`IRunable`）：有 `Main` 入口，层要
- **可安装**（`IAssembly`）：导出 `CreateExtension` 可被动态加载，只有应用根要

`IAssembly` 与 `TSingleton` **互斥**（动态加载可 new 多个 vs 单例唯一），codegen 自动生成 `static_assert` 强制。

### 编译期组装

- `TExtension<TExtensions...>`：依赖表，编译期组合工具列表
- `TFilter<TList, TBase>`：按基类过滤品种（如 `IAssembly`）；`TFilterWhere<TList, TPredicate>` 按谓词（如 `TIsSingleton`）过滤，分半调度
- `ForEach<TList>`：把插件列表摊平成若干次回调，由调度器决定串行或并行——没有运行时容器，没有反射
- 单例直调：`T::Get()` 首次访问才构造

### 接口与实现分离

入口插件的 `Public/` 只放接口（按功能分文件夹），实现写在入口之外的新插件 `Private/` 里。入口不关心实现原理，只负责调度。插件的 Public 头不泄露第三方依赖。
