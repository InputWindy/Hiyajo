#pragma once

// Reflect - refl-cpp compile-time reflection (header-only, MIT). This plugin
// bundles the library so consumers `#include <Reflect.h>` and use `refl::*`
// directly. No singleton, no state - a pure library.
//
//   MAHO_REFLECT(Person, field(name), field(age));      // declare reflectable type
//   refl::for_each(refl::reflect<Person>().members, [](auto m) {
//       std::cout << m.name << "\n";   // "name", "age"
//   });
#include <refl.hpp>

#include <cstddef>

// -- reflection macro sugar ---------------------------------------------------

/**
 * Declare a reflectable type (refl-cpp REFL_AUTO wrapper). `fields` is a
 * `field(member), field(member), ...` comma list; `bases<>` may be added.
 *
 * WARNING: MUST be expanded in the GLOBAL namespace - refl-cpp injects
 * `namespace refl_impl::metadata` at global scope; inside a namespace it
 * specializes in the wrong place and compilation fails with
 * "does not support reflection".
 *
 *   MAHO_REFLECT(Person, field(name), field(age));      // global
 *   MAHO_REFLECT(FUnit, bases<FEntity>, field(hp));     // global
 */
#define MAHO_REFLECT(Type, ...) \
	REFL_AUTO(type(Type), __VA_ARGS__)

/**
 * Iterate all members of a reflectable type. The member descriptor has `.name`
 * (const char*), `.value`, `.is_readable()` / `.is_writable()` (member
 * functions use `.callable()`). The second parameter is the compile-time index
 * (std::size_t).
 *
 *   MAHO_FOR_EACH_MEMBER(Person, [](auto member, auto index) {
 *       std::printf("%zu: %s\n", index, member.name);
 *   });
 */
#define MAHO_FOR_EACH_MEMBER(Type, Visitor) \
	refl::util::for_each(refl::reflect<Type>().members, Visitor)

/** Member count of a reflectable type (compile-time). */
#define MAHO_MEMBER_COUNT(Type) \
	refl::reflect<Type>().members.size

/** Member descriptor of a reflectable type (refl::reflect<Type>()). */
#define MAHO_MEMBER_DESCRIPTOR(Type) \
	refl::reflect<Type>().members

/**
 * Iterate readable field members of a reflectable type (skips member
 * functions). Callback signature is `[](auto field, auto index) { ... }`;
 * field has `.name` / `.value`.
 *
 *   MAHO_FOR_EACH_FIELD(Person, [](auto field, auto index) {
 *       std::printf("field %s\n", field.name);
 *   });
 */
#define MAHO_FOR_EACH_FIELD(Type, Visitor) \
	refl::for_each(refl::reflect<Type>().readable_members, Visitor)

