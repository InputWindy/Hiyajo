#pragma once

#include <Core/Async/ThreadPool.h>
#include <Core/Async/ThreadedServer.h>
#include <Core/Async/Runable.h>

// Async module aggregate: the two parallel models + the runnable contract.
//
//   FThreadPool       — transient parallel tasks (ForEach, jobs, short compute)
//   FThreadedServer   — dedicated resident worker (render / IO / audio role)
//   IRunable          — anything with a MainLoop (engine, threaded server)
