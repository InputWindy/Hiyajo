# Core Server

Threaded task execution and async transfer primitives.

## Modules

| Header | Purpose |
|--------|---------|
| `ThreadedServer.h` | `FThreadedServer`: persistent worker + FIFO task queue + context arena |
| `AsyncTransferServer.h` | `TAsyncTransferServer<TRequest, TResult>`: submit/retrieve async pipeline |
| `ServerTask.h` | `FServerTask` base + `FLambdaServerTask` convenience task |
| `TaskContext.h` | `FTaskContext` base + opaque `FTaskContextId` |
| `TransferHandle.h` | `FTransferHandle` transport ticket + process-wide transfer table |

Use `Server.h` to include all Server headers at once.

## Use

```cpp
#include <Core/Server/Server.h>

struct FLoadContext : Maho::FTaskContext
{
    std::string Path;
};

Maho::FTaskContextId Id = Server.AllocContext<FLoadContext>();
Server.GetContextAs<FLoadContext>(Id)->Path = "A.png";
Server.Enqueue(std::make_unique<Maho::FLambdaServerTask>(Id,
    [Id](Maho::FThreadedServer& S)
    {
        auto* Ctx = S.GetContextAs<FLoadContext>(Id);
        // load Ctx->Path on the worker thread
    }));
Server.Flush();
```

## Related docs

- [Core Engine](../Engine/README.md)
- [Misc infrastructure](../Misc/README.md)
- [Core aggregate](../Core.h)
