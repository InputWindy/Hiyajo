# Public

## 代码文件

- [CommandParser.h](CommandParser.h)
- [CommandParserApi.h](CommandParserApi.h)

## 接口字典

| 声明 | 说明 |
|------|------|
| `FCommandParser : TExtensionList<FCommandParser>` | 解析器单例（纯单例，无 Main/IAssembly） |
| `ECommandParserStage` | 本插件自定义 drive stage（Init / Shutdown） |
| `FCommandParser::ExecuteStage(ECommandParserStage)` | 阶段分发：清空 store |
| `FCommandParser::Find(std::string_view)` | 按名查值；不存在返回 `nullptr` |
| `FCommandParser::Has(std::string_view)` | 键是否存在（含无值开关） |
| `FCommandParser::Count()` | 已解析条目数 |
| `FCommandParser::Reset()` | 清空 store |
| `ParseCommandLine(int, char**)` | 解析 argc/argv 进 store（幂等，后调用覆盖） |

## 相关文档

- [../CommandParser.md](../CommandParser.md) — 概念 + 用法
- [../Private/PrivateDoc.md](../Private/PrivateDoc.md) — 实现算法字典
