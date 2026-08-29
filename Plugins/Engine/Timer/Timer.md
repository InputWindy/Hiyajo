# Timer

## Code Files

- [Timer.h](Timer.h) - timing and clock (`FTimer` / `FScopedTimer` / `FGameClock`)

## Concept - Timing Analysis + Game Clock

Timer plugin provides **two independent singletons** (`FTimer` + `FGameClock`): `FTimer` is a stack-based scope timer (hierarchical timing instrumentation); `FGameClock` is a real/game clock with time scale / pause (lazily advanced, no per-frame Tick required).

### FTimer - Hierarchical Scope Timing (Singleton Service)

`TSingleton<FTimer>` + `IPlugin<IInit, IShutdown>`. Internal `FNode` tree (Root -> Children map): `BeginScope(Name)` pushes down (same-name nodes accumulate); `EndScope()` pops up and accumulates elapsed (Total/Max/Count); `DumpToString()` outputs a millisecond-indented text tree. Initialize/Shutdown/Reset clear the tree.

```cpp
void Render()
{
    Timer::FScopedTimer Scope("Render");   // BeginScope on construction
    // ... work ...
}                                          // EndScope on destruction
Timer::FTimer::Get().DumpToString();       // "Render: 1.23 ms (n calls, avg, max)"
```

`FScopedTimer` is an RAII wrapper - `BeginScope` on construction, `EndScope` on destruction (non-copyable).

### FGameClock - Game Clock (Singleton Service)

`TSingleton<FGameClock>` + `IPlugin<IInit, IShutdown>`. **Lazy advance**: `GetGameSeconds()` accumulates, on call, "wall-clock delta since last call x TimeScale"; no per-frame Tick required; freezes while paused (advance once before `SetPaused(true)`). `GetDeltaSeconds()` returns the game-time delta of the previous advance.

```cpp
const double Real = FGameClock::Get().GetRealSeconds();
const double Game = FGameClock::Get().GetGameSeconds();
FGameClock::Get().SetTimeScale(0.5);   // slow motion
FGameClock::Get().SetPaused(true);     // freeze
```

## Third-Party Dependencies

- None (pure std, `std::chrono`).

## Related Docs

- [API.html](API.html) - API docs
