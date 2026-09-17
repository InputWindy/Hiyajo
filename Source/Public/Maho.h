#pragma once

// Maho -- engine aggregate header. Include this file to get the Core infrastructure + the Engine
// frame system. It is the single aggregate: the Core headers are listed here rather than through a
// second aggregate, because a per-module aggregate whose only consumer is this file is pure
// indirection (and one more place for the list to drift).

#include <Core/TypeList.h>
#include <Core/Delegate.h>
#include <Core/Singleton.h>
#include <Core/Interface.h>
#include <Core/FrameGraph.h>
#include <Core/ThreadPool.h>
#include <Core/ThreadedServer.h>
#include <Core/Assembly.h>
#include <Core/Fatal.h>
#include <Engine/Engine.h>
