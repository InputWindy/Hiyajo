#pragma once

// Reflect — refl-cpp compile-time reflection (header-only, MIT). This plugin
// bundles the library so consumers `#include <Reflect.h>` and use `refl::*`
// directly. No singleton, no state — a pure library.
//
//   struct Person { std::string name; int age; };
//   refl::for_each(refl::reflect<Person>().members, [](auto m) {
//       std::cout << m.name << "\n";   // "name", "age"
//   });
#include <refl.hpp>
