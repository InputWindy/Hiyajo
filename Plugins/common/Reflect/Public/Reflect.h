#pragma once

// Reflect — refl-cpp compile-time reflection (header-only, MIT). This plugin
// bundles the library so consumers `#include <Reflect.h>` and use `refl::*`
// directly. No singleton, no state — a pure library.
//
//   MAHO_REFLECT(Person, field(name), field(age));      // 声明可反射类型
//   refl::for_each(refl::reflect<Person>().members, [](auto m) {
//       std::cout << m.name << "\n";   // "name", "age"
//   });
#include <refl.hpp>

#include <cstddef>

// ── 反射宏语法糖 ────────────────────────────────────────────────────────

/**
 * 声明一个可反射类型（refl-cpp 的 REFL_AUTO 包装）。fields 是
 * `field(成员名), field(成员名), ...` 逗号列表；可加 `bases<>` 等。
 *
 * ⚠️ **必须在全局命名空间展开**——refl-cpp 注入 `namespace refl_impl::metadata`
 * 于全局；在命名空间内会特化到错误位置，编译报 "does not support reflection"。
 *
 *   MAHO_REFLECT(Person, field(name), field(age));      // 全局
 *   MAHO_REFLECT(FUnit, bases<FEntity>, field(hp));     // 全局
 */
#define MAHO_REFLECT(Type, ...) \
	REFL_AUTO(type(Type), __VA_ARGS__)

/**
 * 遍历可反射类型的全部成员。member 描述符含 `.name`（const char*）、`.value`、
 * `.is_readable()` / `.is_writable()`（成员函数则为 `.callable()`）。第二个参数
 * 是编译期索引（std::size_t）。
 *
 *   MAHO_FOR_EACH_MEMBER(Person, [](auto member, auto index) {
 *       std::printf("%zu: %s\n", index, member.name);
 *   });
 */
#define MAHO_FOR_EACH_MEMBER(Type, Visitor) \
	refl::util::for_each(refl::reflect<Type>().members, Visitor)

/** 可反射类型的成员数量（编译期）。 */
#define MAHO_MEMBER_COUNT(Type) \
	refl::reflect<Type>().members.size

/** 可反射类型的类型描述符（refl::reflect<Type>()）。 */
#define MAHO_MEMBER_DESCRIPTOR(Type) \
	refl::reflect<Type>().members

/**
 * 遍历可反射类型中可读的字段成员（排除成员函数）。回调签名为
 * `[](auto field, auto index) { ... }`，field 有 `.name` / `.value`。
 *
 *   MAHO_FOR_EACH_FIELD(Person, [](auto field, auto index) {
 *       std::printf("field %s\n", field.name);
 *   });
 */
#define MAHO_FOR_EACH_FIELD(Type, Visitor) \
	refl::for_each(refl::reflect<Type>().readable_members, Visitor)

