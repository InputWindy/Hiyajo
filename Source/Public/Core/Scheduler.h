#pragma once

#include <Core/Topology.h>

namespace Maho
{

/** Serial traversal — run every callable in order (no thread, stateless). */
struct FSerialTraversePolicy
{
	template <typename... FCallables>
	void Run(FCallables&&... Callables) const
	{
		(Callables(), ...);
	}
};

/**
 * Scheduler base — declares the Run + Execute contracts.
 *
 * Run    — drives a set of callables (serial / parallel — derived policy).
 * Execute — drives a group of extensions by dependency levels: within a level
 *           the extensions run by the derived policy, between levels serially.
 *
 * Stage is a non-type template parameter (auto), so the base needs no stage
 * type. Core only declares the contracts; the base functions are deleted,
 * every concrete scheduler shadows them with its own implementation.
 */
class IScheduler
{
public:
	virtual ~IScheduler() = default;

	/** Drive every callable (serial / parallel — derived policy). */
	template <typename... FCallables>
	void Run(FCallables&&... Callables) const = delete;

	/** Drive the extensions by stage (calls ExecuteStage(Stage) per level). */
	template <auto Stage, typename TExtensions, typename TTopology = FForwardTopology>
	void Execute() = delete;

	/** Drive the extensions by lambda (Visitor decides what to call — no stage). */
	template <typename TExtensions, typename TVisitor>
	void Execute(TVisitor&& Visitor) = delete;
};

} // namespace Maho
