# Public

## 代码文件

- [Config.h](Config.h)
- [ConfigApi.h](ConfigApi.h)

## 接口字典

| 声明 | 说明 |
|------|------|
| `FConfig : TExtensionList<FConfig>` | 配置单例（纯单例，无 Main/IAssembly） |
| `FConfig::ExecuteStage(EConfigStage)` | 阶段分发（Init / Shutdown 均清空 section） |
| `EConfigStage` | 本插件自定义 drive stage |
| `Load(string_view)` | 解析 INI 文件，失败返回 false |
| `GetString(Section, Key)` | 原始字符串查找，缺失返回 nullopt |
| `GetInt / GetFloat / GetBool` | 类型化取值，回退 Default |
| `SetString(Section, Key, Value)` | 运行时覆盖 |
| `HasSection / HasKey` | 存在性查询 |

## 相关文档

- [../Config.md](../Config.md) — 概念 + 用法
- [../Private/PrivateDoc.md](../Private/PrivateDoc.md) — 实现算法字典

