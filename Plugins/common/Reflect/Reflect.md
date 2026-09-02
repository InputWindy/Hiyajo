# Reflect

## Code files

- [Reflect.h](Public/Reflect.h) — refl-cpp 打包 + 反射宏糖（`MAHO_REFLECT` / `MAHO_FOR_EACH_MEMBER` / `MAHO_MEMBER_COUNT` / `MAHO_MEMBER_DESCRIPTOR` / `MAHO_FOR_EACH_FIELD`）
- [Reflect.cpp](Private/Reflect.cpp) — header-only 占位 TU（include + 编译期 smoke 测试，保证 codegen 目标携带 include 目录与 `reflcpp::reflcpp` 传递链接）

## Concept - 编译期反射宏糖

打包 refl-cpp（v0.12.4，header-only，MIT）为引擎统一的反射入口。**纯库**：无单例、无状态。消费方 `#include <Reflect.h>` 后直接用宏或 `refl::*`。**`MAHO_REFLECT` 必须在全局命名空间展开**——refl-cpp 在全局注入 `namespace refl_impl::metadata`；在命名空间内特化错位，编译报 "does not support reflection"。

### 声明可反射类型

`MAHO_REFLECT(Type, field(member), ...)` 包装 `REFL_AUTO`；可用 `bases<>` 声明基类。

### 遍历与查询

- `MAHO_FOR_EACH_MEMBER` / `MAHO_FOR_EACH_FIELD`：遍历全部成员 / 仅可读字段。
- `MAHO_MEMBER_COUNT` / `MAHO_MEMBER_DESCRIPTOR`：编译期成员数 / 描述符集合。

```cpp
MAHO_REFLECT(Person, field(name), field(age));          // global scope!
refl::for_each(refl::reflect<Person>().members, [](auto m) {
    std::cout << m.name << "\n";                        // "name", "age"
});
MAHO_FOR_EACH_MEMBER(Person, [](auto member, auto index) {
    std::printf("%zu: %s\n", index, member.name);
});
static_assert(MAHO_MEMBER_COUNT(Person) == 2);
```

## Third-party dependencies

- **refl-cpp** v0.12.4（`github.com/veselink1/refl-cpp`，FetchContent 拉取，header-only，MIT）— 编译期反射库。

## Related docs

- [API.md](API.md) - API documentation
