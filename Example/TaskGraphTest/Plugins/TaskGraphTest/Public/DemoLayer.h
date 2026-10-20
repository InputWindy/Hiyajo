/// @file
/// @brief THE APPLICATION LAYER -- what a Maho developer writes, and nothing else.
///
/// This file has no test code in it: it is one layer's contract (a task), one class that implements it, and the
/// declaration of what that implementation touches. Read it top to bottom -- it is the whole story:
///
///   1. `MAHO_DECLARE_TASK`      the INTERFACE: one pure virtual per call shape
///   2. a class that inherits it  "I can run this task", checked by the compiler per shape
///   3. `MAHO_PROPERTY`           the MEMORY: storage private, `Get()`/`Set()` only, metadata included
///   4. `MAHO_DECLARE_TASK_BINDING` what that implementation touches on that shape, next to the code it
///      describes
///
/// What is NOT here, on purpose: no key, no edge, no thread, no frame number, no buffer, no submit. Those belong
/// to the layer's frame (`Core/Task/TaskDispatch.h`), and they are the same for every task of every layer.
#pragma once


#include <Core/Task/AccessDerivation.h>
#include <Core/Task/Task.h>
#include <Core/Task/TaskDispatch.h>

namespace Maho
{
namespace DemoLayer
{

// ── (1) the interface: a task FOR one method, one declaration per call shape ─────────────────────────────
//
// `Tick`/`Swarm` is the METHOD; `ISwarm` is the type the method's implementations inherit. Each shape below
// becomes one pure virtual (`void Swarm(float)`, `void Swarm(float, int)`), so an owner that inherits this and
// forgets one stays abstract -- the compiler is what enforces "implement the whole interface".
MAHO_BEGIN_DECLARE_TASK(Swarm)
	MAHO_DECLARE_TASK_METHOD_OVERRIDE(Swarm, (float));
	MAHO_DECLARE_TASK_METHOD_OVERRIDE(Swarm, (float, int));
MAHO_END_DECLARE_TASK(Swarm)

// ── (2) the implementation ───────────────────────────────────────────────────────────────────────────────
//
/// One light. Inheriting `ISwarm` is the statement; the two `override`s are the compiler checking it.
///
/// Its memory is PROPERTIES: the value is private, `Get()`/`Set()` are the only way in -- which is also the only
/// way an access can be OBSERVED (the Debug honesty check is what that buys). A plain member like `Drag` has no
/// region at all, so it is outside the conflict model: only properties take part in read/write ordering.
struct FLight : ISwarm
{
	MAHO_PROPERTY(float, Position);
	MAHO_PROPERTY(float, Velocity);

	/** A plain member: NOT monitored. If it ever has to be ordered against, make it a property. */
	float Drag = 1.0f;

	/** How many times this light ran (a test counter, not model state). */
	int Steps = 0;

	void Swarm(float Dt) override;
	void Swarm(float Dt, int Times) override;
};

/// The definitions can live next to the declaration or out of line -- both are ordinary C++ (`override` stays on
/// the declaration, never repeated here).
/// @param Dt The frame's delta time.
void FLight::Swarm(float Dt)
{
	// The MODE is spelled at the use site: `MAHO_WRITE`/`MAHO_READ` are the markers a scanner (or a reviewer)
	// can see, and `MAHO_WRITE` on a const object simply does not compile.
	MAHO_WRITE(Position) += MAHO_READ(Velocity) * Dt * Drag;
	++Steps;
}

/// @param Dt The frame's delta time.
/// @param Times How many times to advance.
void FLight::Swarm(float Dt, int Times)
{
	for (int Index = 0; Index < Times; ++Index)
	{
		Swarm(Dt);
	}
}

// ── (3) what this implementation touches on ONE shape, and nothing else ──────────────────────────────────
//
// The declaration belongs to the IMPLEMENTATION (another class implementing the same shape may touch completely
// different memory), it is written next to the code it describes, and it names the MEMORY: `MAHO_MEMORY_ACCESS_*`
// appends the `Region` suffix itself, so a property is named by its own name.
//
// It also names the NODE TYPE (`FLightSwarm`): the recorder says `Record<FLightSwarm>()`, and the frame's data
// (`Light`, `Dt`) is supplied at submit -- never stored in the recording.
MAHO_DECLARE_TASK_BINDING(FLightSwarm, FLight, Swarm, (float),
	MAHO_MEMORY_ACCESS_WRITE(FLight::Position),
	MAHO_MEMORY_ACCESS_READ(FLight::Velocity));

// The SECOND shape of the SAME method is a different node TYPE (its own binding, its own key). Its accesses are
// the same as the first one's, so it INHERITS them from that binding instead of repeating them -- a binding
// exposes its `List`, exactly like a declaration does.
MAHO_DECLARE_TASK_BINDING(FLightSwarmTimes, FLight, Swarm, (float, int),
	MAHO_ACCESS_INCLUDE(FLightSwarm));

// ── (4) the SAME declaration, as a scanner would WRITE IT ────────────────────────────────────────────────
//
// A tool cannot paste `FLight::PositionRegion` (it would have to know the naming rule); what it CAN do is copy
// the member expression it read from the body -- `MAHO_WRITE(Position)` inside `FLight::Swarm` -- into
// `decltype(...)::FRegion`. That names exactly the same region, needs no namespace and no type inference, and
// the access control of `decltype` is why a property must be PUBLIC (see `MAHO_PROPERTY`).
//
// This block is the observed-and-verified shape of a GENERATED declaration (see test B38): it must describe the
// same memory as the binding above, or the generator is wrong.
MAHO_DECLARE_ACCESSES(FLightSwarmScanned,
	MAHO_MEMORY_ACCESS(decltype(FLight::Position)::FRegion, ::Maho::Core::EMemoryAccess::Write),
	MAHO_MEMORY_ACCESS(decltype(FLight::Velocity)::FRegion, ::Maho::Core::EMemoryAccess::Read));

}
}
