# Name

## Code files

- [Name.h](Name.h) - string interning identifier (`FName` + `FNamePool` + `std::hash` specialization)

## Concept - string interning

The Name plugin provides **immutable string identifiers** - constructing an `FName` interns the string into the global pool, identical strings share one entry, comparison is O(1) (by internal Id, no byte-by-byte comparison). Frequently repeated engine strings (resource directory keys, CVar names, etc.) use FName to avoid duplicate storage and comparison cost.

### FName - interned identifier

Default-constructed = None (empty, `Id == 0`). Explicit construction `FName("head")` goes through `FNamePool::Get().Intern`; `ToString()` reverses the lookup into the pool.

```cpp
const FName Bone = "head";
const FName Also = "head";       // same pool entry
Bone == Also;                    // true, O(1)
```

A matching `std::hash<FName>` specialization (by `GetId()`) - usable directly as an `unordered_map` key.

### FNamePool - global interning pool (singleton service)

`TSingleton<FNamePool>` + `IPlugin<IInit, IShutdown>`, `Mutex` protected, thread-safe. `Intern` is idempotent (returns the canonical entry when it exists), `StringForId` reverses. Initialize/Shutdown clear the pool (`free()`).

## Third-party dependencies

- None (pure std).

## Related docs

- [API.html](API.html) - API documentation
