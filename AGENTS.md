# Maho — Agent 入口（所有 AI Agent 的第一站）

所有 AI Agent（Claude Code / Codex / Copilot / Cursor 等）都通过本文件作为入口了解本项目。
面向人类的引擎总览见根目录 `README.md`。

## 代码设计约束（强约束）

以下规则对任何代码改动都强制生效。

### 1. 编码风格

照抄 Unreal Engine C++ 编码规范。唯一区别：

- 不引入 UE 专属的类型前缀（`U`/`A` 等，即 `UObject`/`AActor` 那套继承层级前缀），**具体类/结构体统一 `F` 前缀**（`FEngineBase`、`FWorld`、`FTexture`）。
- 功能性前缀沿用 UE：接口 `I`（`IEngineExtension`）、枚举 `E`（`EEngineStage`）、模板 `T`（`TAsyncTransferServer`）、bool `b`。
- 其余沿用 UE 规范：大驼峰（PascalCase）命名、字段命名、Allman 括号、Tab 缩进、`.h`/`.cpp` 内只用英文注释、`FWorld* World` / `const FWorld& World` 的指针引用写法。

### 2. Public / Private 划分

- 代码文件分 `Public/` 和 `Private/` 两个目录。
- `Public/` 只放该功能对外暴露的**接口**和**数据结构声明**。
- 功能实现代码全部放 `Private/`。
- 某个类若只是功能模块的内部实现、不需要被外部感知，则整个写进 `Private/`。

### 3. 目录组织 + 聚合头

- `Public/`、`Private/` 下不摊平，按功能相近的合并放在次级文件夹下。
- `Public/` 根目录放一个**与功能模块名相同的聚合头**，负责统一 include 该功能下必要的 public 头。
- 其他功能模块引用该模块时，只 include 那个同名头，语义清晰。

### 4. 用模块前先读文档

- 引用某功能模块的代码实现功能前，**必须先去学习那个模块的 README**。
- 文档会教你怎么引用该功能、怎么拓展。

### 5. 最简指令集

- 类和接口设计必须遵守最简指令集的思路，**非必要不要管闲事**（不越界、不加多余职责/功能）。

### 6. 多态 + 访问控制 + 代码分区

- 多用多态来设计类和接口：用抽象接口类 + base 类拆解功能、分层隔离字段/属性/功能接口。
- 函数和字段不能一股脑写成 public。优先级：**private 优先 → protected 其次 → 万不得已才 public**。
- 类内函数和字段按功能分区写在一块，不随意乱插入代码。

### 7. 多态实现优先级

- **模板元编程最优先**：可变模板参数、模板偏特化、编译器（编译期）计算。
- 模板元编程实现不了的功能，才下放到**运行时多态**（虚函数 + 继承）。

### 8. 组装优先于继承

- 类设计优先考虑**组装式**：把模块拆成"排插"+"插头"，通过传**可变模板参数列表**往排插类上组装功能。
- 无法组装，才退而求其次使用继承。
- 总结：设计类时把功能做分类，每一类想办法用模板类的方式插进来；模板不行再用继承。

### 9. 文档覆盖

- 每个类和接口都应该能在某个文档里找到介绍。
- 每个功能模块在**聚合头所在的同级目录**下维护**一组文档**：`.md`（流程/概念解释）+ `.html`（API 文档），与聚合头 `.h` 构成"一个头 + 两份文档"。

### 10. 文档写法

- 写文档要**少写文字，多插流程图**（用 Python 绘制的图，暗色底风格）。
- 文档里必须**标明可跳转的其他文档路径**（相对链接）。
- 文档和代码一样跟着功能模块走：引用其他功能模块时，要在自己 README 里补上被引用模块 README 的链接。
- **文档分工**：`.md` 写流程/概念解释（选型、对比、流程图、用法示例）；`.html` 写 API 文档（类定义 + 函数签名 + 参数/返回值/线程安全等解释）。
- **`.html` API 文档风格**（统一模板）：
  - 暗色底：背景 `#14181f`、类面板 `#1b2130`、代码块 `#0d1117`
  - 每个类一个**带边框的类面板**（圆角 + 左侧色条），类名用大号 monospace + `[class]` 徽章
  - 用**表格分类**：`接口`表（签名 | 说明）、`成员变量`表（字段 | 类型 | 说明）
  - 签名做语法高亮（关键字 `kw` / 类型 `type` 配色），修饰符加徽章（`virtual`/`protected`/`template`/`static`）
  - 分组标题大写 + 下划线分隔（如 `接口`、`成员变量`）

## 文档路径指引

### 入口

| 文档 | 作用 |
|------|------|
| `AGENTS.md`（本文件） | 所有 Agent 的第一站 |
| `README.md` | 面向人类的引擎总览 |

### 设计文档

- `设计文档/` — 游戏设计文档（如 `梦境游戏剧情策划案.md`）。

### 引擎核心（`Maho/Source/Public`）

- `Maho.h` — 引擎聚合头（统一 include 核心 public 头）。
- `Core/` 下按功能分模块，各模块文档位于对应聚合头同级目录：
  - `Core/Engine/` — `FEngineBase` / `FGameClientEngine` / `FGameServerEngine` / `FNullEngine` / `IEngineExtension` / 引擎 stage。
  - `Core/Misc/` — `Log` / `Json` / `Paths` / `Delegate` / `TypeList` 等基础设施。
  - `Core/Server/` — 线程 / 异步服务器（`TAsyncTransferServer` 等）。

### 插件（`Maho/Plugins`）

| 文档 | 作用 |
|------|------|
| `Maho/Plugins/README.md` | 插件总览 |
| `Maho/Plugins/<Name>/README.md` | 每个插件的引用 / 拓展指南（实现功能前先读） |

插件结构：`Source/<Name>/Public/`（聚合头 + 子模块文档）+ `Source/<Name>/Private/`（实现）。

### 构建 / 工具

| 文档 | 作用 |
|------|------|
| `Build/README.md` | CMake 构建体系 + 项目模板 |
| `Tools/README.md` | 引擎工具链 |

### 三方库拉取镜像源

引擎核心零三方依赖；所有三方库由各插件的 `<Name>.cmake` 用 FetchContent 拉取。遇到 GitHub 拉不下来（大陆网络）时，两种配置方式：

| 方式 | 配置 | 说明 |
|------|------|------|
| 逐仓库镜像 | 编辑 `Maho/Mirrors.txt`，去掉对应行注释 | gitee 官方镜像（稳定），`github路径=镜像url` 每行 |
| 透明代理前缀 | `setx MAHO_GIT_PROXY_PREFIX https://ghproxy.com/` 或 cmake `-DMAHO_GIT_PROXY_PREFIX=...` | 纯前缀拼接，一次性覆盖所有 GitHub 克隆 |

优先级：`Mirrors.txt` 映射 > 代理前缀 > 直连 GitHub。改写统一走 `maho_git_repository_url()`（`Build/CMake/MahoDependencies.cmake`），插件 `.cmake` 里 `FetchContent_Declare` 的 `GIT_REPOSITORY` 都经它。拉取卡住时先杀 `cmake.exe`/`git.exe`，再删 `Intermediate/_deps/<name>-*` 半成品目录重试。
