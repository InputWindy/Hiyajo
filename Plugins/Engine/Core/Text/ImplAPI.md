# Text（Private）— 实现算法字典

cpp 侧每个函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。

## Text.cpp

<a id="fn-text-get"></a>
### GetTextManager()

← [公开 API](API.md) · `FTextManager*`

```text
GetTextManager():
1. return GTextManager
```

<a id="fn-text-makekey"></a>
### MakeKey(Namespace, Key, Culture)（内部）

匿名命名空间。目录键 = 三段以单位分隔符 `\x1f` 连接，避免 `"a"+"bc" == "ab"+"c"` 冲突。

```text
MakeKey(Namespace, Key, Culture):
1. K.reserve(Namespace.size() + Key.size() + Culture.size() + 2)
2. K.append(Namespace); K.push_back('\x1f')
3. K.append(Key);       K.push_back('\x1f')
4. K.append(Culture)
5. return K
```

<a id="fn-text-resolve"></a>
### FText::Resolve() const

← [公开 API](API.md) · `std::string`

经全局管理器查当前文化翻译；管理器缺失或无翻译回退源文本。

```text
Resolve():
1. Manager = GetTextManager()
2. if Manager == nullptr: return Source
3. Translated = Manager->FindTranslation(Namespace, Key, Manager->GetCulture())
4. return Translated ? *Translated : Source
```

<a id="fn-text-init"></a>
### FTextManager::Initialize(FEngineBase&) / Shutdown(FEngineBase&)

← [公开 API](API.md) · `void`

```text
Initialize(Engine):
1. lock(Mutex): Catalog.clear(); CurrentCulture = "en-US"; GTextManager = this

Shutdown(Engine):
1. lock(Mutex): Catalog.clear(); GTextManager = nullptr
```

<a id="fn-text-culture"></a>
### FTextManager::GetCulture / SetCulture

← [公开 API](API.md) · `std::string_view` / `void`

```text
GetCulture():
1. lock(Mutex): return CurrentCulture

SetCulture(InCulture):
1. lock(Mutex): CurrentCulture = move(InCulture)
```

<a id="fn-text-add"></a>
### FTextManager::AddTranslation(Namespace, Key, Culture, Text)

← [公开 API](API.md) · `void`

```text
AddTranslation(Namespace, Key, Culture, Text):
1. lock(Mutex): Catalog[MakeKey(Namespace, Key, Culture)] = move(Text)
```

<a id="fn-text-json"></a>
### FTextManager::LoadTranslationsFromJson(std::string_view JsonText)

← [公开 API](API.md) · `void`

解析 JSON 对象数组，逐条 AddTranslation。

```text
LoadTranslationsFromJson(JsonText):
1. Root = nlohmann::json::parse(JsonText)
2. for Entry in Root:
       AddTranslation(Entry.at("Namespace"), Entry.at("Key"), Entry.at("Culture"), Entry.at("Text"))
```

<a id="fn-text-find"></a>
### FTextManager::FindTranslation(Namespace, Key, Culture) const

← [公开 API](API.md) · `const std::string*`

```text
FindTranslation(Namespace, Key, Culture):
1. lock(Mutex)
2. It = Catalog.find(MakeKey(Namespace, Key, Culture))
3. return It != end ? &It->second : nullptr
```

<a id="fn-text-export"></a>
### CreateLayer()（C 导出）

```text
extern "C" CreateLayer():
1. return Maho::Text::FTextManager::CreateLayer()
```

- [Text.md](Text.md) — 概念 · [公开 API](API.md) — 签名入口
