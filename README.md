# Maho

<div style="background:#143d2b;border:1px solid #3f8f63;border-radius:8px;padding:12px 16px;color:#cdeadd;">

<b>作者寄语：Coding前请务必让AI通读 AGENTS.md 学习规范 —— 因为写代码的是AI，而不是程序员。</b>

</div>

## 简介

Maho 是一个纯 C++20 的游戏引擎，核心设计围绕**并行调度**与**插件式架构**展开。它借鉴 Unity DOTS 的理念：把应用拆解成一组职责单一的功能层，由调度器在每一帧自动编排执行——跨层串行、同层并行，层间依赖自动插入同步点。

一个功能就是一个插件，一个插件就是一个 DLL。引擎核心只提供少量通用积木，所有能力——日志、序列化、物理、渲染——都以插件形式存在，按需安装。

## 特性

- **并行调度**：`IScheduler` 双模式驱动——按阶段（stage）分发，或按 lambda 分发。同层插件在自适应的线程池上并行执行，层间屏障同步。
- **插件即一切**：工具、功能模块、应用程序，都是插件。宿主本身也是插件，支持递归嵌套。
- **编译期组装**：插件组合、依赖排序、遍历展开全部在编译期由模板完成，运行时零反射、零排序开销。
- **单例直调**：所有插件是懒加载单例，`T::Get()` 触发构造，无 `new`、无运行时容器。
- **零三方依赖的核心**：引擎核心不含任何第三方库，每个插件在自己的 `.cmake` 里用 FetchContent 拉取所需依赖，镜像与代理可配置。
- **纯通用核心**：核心不预设 stage 枚举、不预设应用形态，全部下放插件层。

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
  <项目>/Public/             ← 入口插件接口
  <项目>/Private/            ← 入口插件实现
  <子插件>/Public/           ← 功能插件（平铺项目根）
  Extension/                  ← 三方插件目录
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

### 插件

一个功能就是一个插件。日志、网络、渲染、物理、脚本……都是平级的功能层，在主循环里被调度器按帧驱动。

- **引擎插件**：Maho 自带一组通用插件（Log / Json / Math / Physics ……），建项目时勾选即可安装。
- **三方插件**：外部获取的现成插件，整个拷进项目 `Extension/` 目录，构建时自动接入。
- **项目插件**：自己写的功能，用 `CreatePlugin.bat` 创建，自动继承引擎插件与三方插件。

### 并行调度

一组平级插件，调度器遍历执行；插件之间有依赖时，显式声明依赖关系，调度器自动插入同步节点。

- 外层跨层串行，同层内并行，线程池自适应（构造零线程，按需懒启动到 CPU 核数上限）。
- 屏障同步：每层执行完才进入下一层；任务异常不会卡死屏障，跑完统一抛出。
- 两种驱动方式：按 stage 分发（`ExecuteStage`），或按 lambda 分发（自定义回调）。

### 沙漏依赖

```
引擎插件 ──┐
          ├──→ 项目入口插件 ──┬──→ 功能子插件
三方插件 ──┘                 ├──→ 更多子插件
                            └──→ ...
```

项目入口插件是唯一的"枢纽"——它只依赖引擎插件与三方插件，而项目自己写的功能子插件都依赖它。依赖方向单向，永无环。

### 编译期组装

- `TExtensionList<TDerived, TExtensions...>`：编译期组合插件，同时是"可被驱动的单例"和"一组扩展"。
- `Topology.h`：每个插件声明一层直接依赖，编译期递归展开传递依赖，算出分层（哪些可并行、哪些要串行）。
- `ForEach<TList>`：把插件列表摊平成若干次回调，由调度器决定串行或并行——没有运行时容器，没有反射。
- 单例直调：所有插件是懒加载单例，`T::Get()` 首次访问才构造。

### 接口与实现分离

入口插件的 `Public/` 只放接口（按功能分文件夹），实现写在入口之外的新插件 `Private/` 里。入口不关心实现原理，只负责调度。插件的 Public 头不泄露第三方依赖。
