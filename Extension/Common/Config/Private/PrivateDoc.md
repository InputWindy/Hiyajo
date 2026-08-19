# Private

## 代码文件

- [Config.cpp](Config.cpp)

## 实现算法字典

Config 插件的实现集中在 `Config.cpp`——INI 解析器。

| 函数 | 说明 |
|------|------|
| `Trim`（内部） | 去首尾空格/制表符/回车 |
| `FConfig::ExecuteStage` | Init/Shutdown 均 `Sections.clear()` |
| `FConfig::Load` | 逐行解析：跳过空行/`;`/`#` 注释；`[section]` 切段；`key=value` 存入 map |
| `FConfig::GetString` | 二级 map 查找，缺失返回 nullopt |
| `FConfig::GetInt` | `std::stoll`，异常回退 Default |
| `FConfig::GetFloat` | `std::stod`，异常回退 Default |
| `FConfig::GetBool` | 小写后匹配 `true/1/yes/on` |
| `FConfig::SetString` | 覆盖写入 section[key] |
| `FConfig::HasSection / HasKey` | 存在性检查 |

## 相关文档

- [../Config.md](../Config.md) — 概念 + 用法
- [../Public/PublicDoc.md](../Public/PublicDoc.md) — 接口层

