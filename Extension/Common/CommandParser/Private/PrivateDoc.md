# Private

## 代码文件

- [CommandParser.cpp](CommandParser.cpp)

## 实现算法字典

| 函数 | 说明 |
|------|------|
| `FCommandParser::ExecuteStage(ECommandParserStage Stage)` | `Init`/`Shutdown` 都 `Storage.clear()` |
| `FCommandParser::Find(std::string_view Name)` | `Storage.find`，命中返回 `&It->second`，否则 `nullptr` |
| `FCommandParser::Has(std::string_view Name)` | 是否 `find != end` |
| `FCommandParser::Count()` | `Storage.size()` 转 int |
| `FCommandParser::Reset()` | `Storage.clear()` |
| `ParseCommandLine(int Argc, char** Argv)` | 跳过 `-`/`--`，按 `=` 或空格切分 name/value，写入 store（幂等） |

## 相关文档

- [../CommandParser.md](../CommandParser.md) — 概念 + 用法
- [../Public/PublicDoc.md](../Public/PublicDoc.md) — 接口层
