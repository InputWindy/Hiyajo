# Reflect — API 文档

Reflect 插件 = refl-cpp 编译期反射打包（header-only，MIT）。纯库形态：无单例、无状态，只提供宏糖——消费方 `#include <Reflect.h>` 后可直接用 `refl::*` 或宏。**`MAHO_REFLECT` 必须在全局命名空间展开**：refl-cpp 会在全局注入 `namespace refl_impl::metadata`，在命名空间内特化错位，编译报 "does not support reflection"。

## MAHO_REFLECT(Type, ...) <宏>

声明可反射类型（refl-cpp `REFL_AUTO` 包装）。`fields` 是 `field(member), field(member), ...` 逗号列表；可加 `bases<>`。**必须在全局命名空间展开**。

```cpp
MAHO_REFLECT(Person, field(name), field(age));      // global scope!
MAHO_REFLECT(FUnit, bases<FEntity>, field(hp));     // global scope!
```

#### 宏

| 签名 | 说明 |
|------|------|
| `MAHO_REFLECT(Type, ...)` | 展开为 `REFL_AUTO(type(Type), __VA_ARGS__)` |

## MAHO_FOR_EACH_MEMBER(Type, Visitor) <宏>

遍历类型的全部成员。成员描述符有 `.name`（`const char*`）/ `.value` / `.is_readable()` / `.is_writable()`（成员函数用 `.callable()`）。第二参数是编译期索引（`std::size_t`）。

```cpp
MAHO_FOR_EACH_MEMBER(Person, [](auto member, auto index) {
    std::printf("%zu: %s\n", index, member.name);
});
```

#### 宏

| 签名 | 说明 |
|------|------|
| `MAHO_FOR_EACH_MEMBER(Type, Visitor)` | 展开为 `refl::util::for_each(refl::reflect<Type>().members, Visitor)` |

## MAHO_MEMBER_COUNT(Type) <宏>

可反射类型的成员个数（编译期常量）。

#### 宏

| 签名 | 说明 |
|------|------|
| `MAHO_MEMBER_COUNT(Type)` | 展开为 `refl::reflect<Type>().members.size` |

## MAHO_MEMBER_DESCRIPTOR(Type) <宏>

可反射类型的成员描述符集合。

#### 宏

| 签名 | 说明 |
|------|------|
| `MAHO_MEMBER_DESCRIPTOR(Type)` | 展开为 `refl::reflect<Type>().members` |

## MAHO_FOR_EACH_FIELD(Type, Visitor) <宏>

遍历可反射类型的可读**字段**成员（跳过成员函数）。回调签名 `[](auto field, auto index)`；`field` 有 `.name` / `.value`。

```cpp
MAHO_FOR_EACH_FIELD(Person, [](auto field, auto index) {
    std::printf("field %s\n", field.name);
});
```

#### 宏

| 签名 | 说明 |
|------|------|
| `MAHO_FOR_EACH_FIELD(Type, Visitor)` | 展开为 `refl::for_each(refl::reflect<Type>().readable_members, Visitor)` |

- [Reflect.md](Reflect.md) — 概念
