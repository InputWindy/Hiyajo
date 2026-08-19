# Maho



<div style="background:#143d2b;border:1px solid #3f8f63;border-radius:8px;padding:12px 16px;color:#cdeadd;">

<b>作者寄语：Coding前请务必让AI通读 AGENTS.md 学习规范 —— 因为写代码的是AI，而不是程序员。</b>

</div>

## 构建流程

### 0. 首次自举

```bat
Setup.bat
```

拉取/安装引擎本地 Python（`Tools/python` junction → `%LOCALAPPDATA%\Maho\python\tooling`）。优先复用宿主机的 Python（带 tkinter）建 venv，否则下载 python.org installer 静默安装。**全程不依赖系统 PATH 上的 python**。

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
- 扫描引擎 `Extension/` + 项目 `Source/` + `Extension/`，重写 CMakeLists（每个插件一个 DLL target）
- 探测本机 Visual Studio（vswhere）选 CMake 生成器
- 产出 `<项目>.sln`

### 3. 构建

用 Visual Studio 打开 `.sln` 构建，或命令行：

```bat
cmake -S . -B Intermediate -G "Visual Studio 17 2022" -A x64
cmake --build Intermediate --config Debug
```

产物：`Intermediate/Debug/` 下各插件 DLL + `EntryPoint.exe`。构建前自动跑插件依赖环检测（`MahoCheckCycle`）。

### 4. 打包

```bat
package.bat
```

Release 构建并拷贝 exe + 全部 DLL 到 `Packaged/Win64/Release/`。

## 设计哲学

基于Unity DOTS的设计理念 —— 并行调度器（`IScheduler`） + 插件式编程。

### 一切皆插件

（你来大致描述一下插件，并行调度器的好处，尤其是并行调度器，灵感来源于Unity DOTS，是先有并行调度器的设计，才给其设计了一套纯插件式的项目构建范式）

应用程序 = 无数层功能模块的叠加执行
对于一组平级的功能层，可以用并行调度器遍历执行。
对于一组平级的功能层，相互之间有依赖关系。可以通过显式定义依赖关系，让并行调度器自动插入同步节点。

日志系统，网络通信，渲染器，游戏世界，虚拟机这些功能本质都是在一个主循环中Tick，符合并行调度的要求。

数学库，文件系统等非Tick的功能模块，也可以合并为一组层，被并行调度器自动插入同步节点。

每个应用程序都安装一个调度器，然后声明好自己要执行的插件层，在主循环中自动调度即可。
所有的插件不需要关心别人此时此刻在干嘛，只需要关心自己在程序运行的什么阶段需要干什么。

### 沙漏依赖

（这里你讲一下创建项目时的要求就行，主要是让用户把三方cplugin拷到extension目录下，然后用createproject.bat创建项目插件，不要自己乱加文件）

```
Maho引擎提供一组通用的功能插件，在创建项目时可以勾选要用的功能，安装到自己的应用里。
也可以去安装三方插件，在自己的应用里安装。

项目自己创建的所有插件都自动继承引擎插件和三方插件
```

### 编译期组装 + 单例直调

（这里主要解释TExtensionList<Self,...TExtensions>和TSkeduler的概念，并且解释一下Topology.h的原理，模板元编程实现方式是如何实现了编译期插件组装的，以及模板ForEach是如何实现模板并行调度器的）

所有的插件都是懒加载的单例。可以通过 `T::Get()` 单例直调，无 `new`、无运行时容器、无反射。

- `TExtensionList<TDerived, TExtensions...>` 编译期组合插件；`ForEach` + `T::Get()` 单例直调，无 `new`、无运行时容器、无反射。
- 依赖插件只 forward-declare，头文件单向：`宿主 Private → 子插件 → 入口 Public`。
- `IScheduler` 双 Execute：按 stage 分发（`ExecuteStage`），或按 lambda 分发（`FDefaultSlot` 排序 + Visitor 自定义）。

### 纯通用核心

引擎核心零 app 假设、零三方依赖、零 stage 预设——只给积木（类型列表 / 拓扑排序 / 单例 / 调度器 / 扩展 / 动态加载）。stage 枚举、app 形态、线程池策略全部下放插件层。

### 接口与实现分离

入口插件 `Public/` 只定义接口（按功能分层），实现写在入口外的新插件 `Private/`。入口不关心实现原理，只负责调度。
