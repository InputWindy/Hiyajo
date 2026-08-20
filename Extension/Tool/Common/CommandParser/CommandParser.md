# CommandParser

Command-line argument parser extension (key-value store)。

## 扩展类

| 字段 | 值 |
|------|-----|
| Class | `Maho::CommandParser::FCommandParser` |
| Header | `CommandParser.h` |
| Stage | `ECommandParserStage`（本插件自定义） |
| Dependencies | — |

## 说明

命令行参数解析插件（键值存储）。`ParseCommandLine(argc, argv)` 把参数解析进 `FCommandParser` 单例的 store，`Find`/`Has`/`Count` 查询。重复调用幂等（后调用覆盖）。

旧引擎里 `ExecuteStage` 和 `ParseCommandLine` 都是 TODO 空实现——迁移时补齐了真实实现。

## 驱动

宿主用 stage 版 Execute 驱动：

```cpp
// 宿主 Main 里
FParallelScheduler S;
S.Execute<Maho::CommandParser::ECommandParserStage, FExtensions>();
// → 对插件调 T::Get().ExecuteStage(ECommandParserStage{...})
```

`ExecuteStage` 处理两个阶段：

| Stage | 行为 |
|-------|------|
| `ECommandParserStage::Init` | 清空 store |
| `ECommandParserStage::Shutdown` | 清空 store |

## 用法

```cpp
#include <CommandParser.h>
using namespace Maho::CommandParser;

ParseCommandLine(argc, argv);
if (const std::string* V = FCommandParser::Get().Find("level"))
{
	// -level=3 或 -level 3
}
```

支持 `-name=value`、`-name value` 两种形式，以及无值开关 `-name`。

## 三方依赖

无。

## 相关文档

- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典（cpp 函数表）
- [../../../AGENTS.md](../../../AGENTS.md) — 引擎根 Agent 入口
- [../../../Source/Public/Core/CoreDoc.md](../../../Source/Public/Core/CoreDoc.md) — 核心基础设施
