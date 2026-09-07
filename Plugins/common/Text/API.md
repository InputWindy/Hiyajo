# Text — API 文档

Text 插件 = 本地化文本：`FText`（数据句柄）+ `FTextManager`（服务层，`FLayer<6 生命周期阶段>`）。`SetCulture` 选文化，`FText::Resolve` 查当前文化翻译并回退到源文本。JSON 加载依赖 nlohmann。`TextApi.h` 提供 `MAHO_TEXT_API` 导出宏。

## Text.h

### Culture <namespace>

受支持的文化常量。字符串是 UTF-8；"支持"指目录可以容纳它们。

#### 常量

| 常量 | 值 | 说明 |
|------|-----|------|
| `Culture::English` | `"en-US"` | 英语（美国） |
| `Culture::Chinese` | `"zh-CN"` | 简体中文 |
| `Culture::Japanese` | `"ja-JP"` | 日语 |

### FText <class>

本地化文本句柄——存 `{Namespace, Key, Source}`。`Resolve()` 查当前文化的翻译，缺省回退到 Source。**非层**，纯数据类。

#### 接口

| 签名 | 说明 |
|------|------|
| `FText() = default` | 空句柄 |
| `FText(std::string InNamespace, std::string InKey, std::string InSource)` | 构造：命名空间 + 键 + 源文本 |
| `[[nodiscard]] std::string_view GetNamespace() const` | 命名空间 |
| `[[nodiscard]] std::string_view GetKey() const` | 键 |
| `[[nodiscard]] std::string_view GetSource() const` | 源文本 |
| `[[nodiscard]] std::string Resolve() const` | 对当前文化解析——缺翻译回退 Source |
| `bool operator==(const FText&) const` | 按 `{Namespace, Key}` 相等（忽略 Source） |

### GetTextManager <自由函数>

全局文本管理器访问器（跨 DLL）。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_TEXT_API FTextManager* GetTextManager()` | 返回全局 `FTextManager*` |

### FTextManager <class>

本地化管理器：当前文化 + 翻译目录（引擎层）。**线程安全**（内部互斥保护）。

#### 接口

| 签名 | 说明 |
|------|------|
| `[[nodiscard]] std::string_view GetCulture() const` | 当前文化（如 `"en-US"`） |
| `void SetCulture(std::string InCulture)` | 设置当前文化 |
| `void AddTranslation(std::string_view InNamespace, std::string_view InKey, std::string_view InCulture, std::string Text)` | 注册一条翻译（线程安全） |
| `void LoadTranslationsFromJson(std::string_view JsonText)` | 从 JSON 数组加载翻译 |
| `[[nodiscard]] const std::string* FindTranslation(std::string_view InNamespace, std::string_view InKey, std::string_view InCulture) const` | 查翻译；无则 nullptr（线程安全） |

#### 生命周期阶段（6 个 stage）

| 阶段 | 方法 | 行为 |
|------|------|------|
| `IInit` | `Initialize(FEngineBase&)` | 清空目录、重置文化为 `"en-US"`、发布 `GTextManager` |
| `IShutdown` | `Shutdown(FEngineBase&)` | 清空目录、清 `GTextManager` |
| 其余 4 个 | — | no-op |

#### LoadTranslationsFromJson 格式

JSON 对象数组，每项四字段：

```json
[ { "Namespace": "MainMenu", "Key": "Title", "Culture": "zh-CN", "Text": "主菜单" } ]
```

- [Text.md](Text.md) — 概念 · [实现字典](ImplAPI.md) — 算法
