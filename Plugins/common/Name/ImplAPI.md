# Name（Private）— 实现算法字典

cpp 侧每个函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。

## Name.cpp

<a id="fn-name-get"></a>
### GetNamePool()

← [公开 API](API.md) · `FNamePool*`

返回全局指针 `GNamePool`——`Initialize` 置 `this`，`Shutdown` 置空。

```text
GetNamePool():
1. return GNamePool
```

<a id="fn-name-init"></a>
### FNamePool::Initialize(FEngineBase&)

← [公开 API](API.md) · `void`

发布 `this` 并清空池——引擎启动从空池开始。

```text
Initialize(Engine):
1. GNamePool = this
2. free()
```

<a id="fn-name-shutdown"></a>
### FNamePool::Shutdown(FEngineBase&)

← [公开 API](API.md) · `void`

回收：撤回 `this`，清空池。

```text
Shutdown(Engine):
1. GNamePool = nullptr
2. free()
```

<a id="fn-name-ctor"></a>
### FName::FName(std::string_view Str)

← [公开 API](API.md) · `explicit`

构造即驻留：把字符串交给全局池 `Intern`，取回规范 id。

```text
FName(Str):
1. Id = GetNamePool()->Intern(Str).Id
```

<a id="fn-name-tostring"></a>
### FName::ToString() const

← [公开 API](API.md) · `std::string_view`

按 id 取回池中字符串。

```text
ToString():
1. return GetNamePool()->StringForId(Id)
```

<a id="fn-name-stringforid"></a>
### FNamePool::StringForId(std::uint32_t Id) const

← [公开 API](API.md) · `std::string_view`

`Intern` 的逆操作；越界返回空串。

```text
StringForId(Id):
1. return Id < Pool.size() ? Pool[Id] : ""
```

<a id="fn-name-free"></a>
### FNamePool::free()（内部）

清空池与查找表。

```text
free():
1. Pool.clear()
2. Lookup.clear()
```

<a id="fn-name-intern"></a>
### FNamePool::Intern(std::string_view Str)

← [公开 API](API.md) · `FName`

驻留算法（线程安全）：空串 → None；查表命中返回既有 id；未命中追加并登记。

```text
Intern(Str):
1. if Str.empty(): return FName{}          // None 不驻留
2. lock(Mutex):
3.   It = Lookup.find(string(Str))
4.   if It != Lookup.end(): return FName{ It->second }
5.   NextId = Pool.size()
6.   Pool.emplace_back(Str)
7.   Lookup.emplace(Pool.back(), NextId)
8.   return FName{ NextId }
```

<a id="fn-name-createlayer"></a>
### CreateLayer()（extern "C"）

DLL 动态安装入口——宿主按符号名查找。委托静态 `FNamePool::CreateLayer()`（`MAHO_DECLARE_LAYER` 生成）。

```text
CreateLayer():
1. return Maho::Name::FNamePool::CreateLayer()
```

- [Name.md](Name.md) — 概念 · [公开 API](API.md) — 签名入口
