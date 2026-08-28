#include <Core/TaskGraph.h>

namespace Maho
{

void FTaskGraph::Init(std::vector<FNode*> Nodes)
{
	Tasks.clear();
	Lookup.clear();
	Tasks.reserve(Nodes.size());

	for (FNode* Node : Nodes)
	{
		if (Node == nullptr)
		{
			continue;
		}
		const std::size_t Index = Tasks.size();
		Tasks.push_back(FTask{ Node });
		Lookup[{ Node->Name, Node->Stage }] = Index;
	}
}

bool FTaskGraph::Compile()
{
	if (Tasks.empty())
	{
		return true;
	}

	// Reset per-task graph state.
	for (FTask& Task : Tasks)
	{
		Task.Downstreams.clear();
		Task.InitPending = 0;
		Task.Pending = 0;
	}

	// Wire edges: for each node's dependency, find the (name, stage) it points
	// to and record both the outgoing edge and the target's pending count.
	for (std::size_t I = 0; I < Tasks.size(); ++I)
	{
		FTask& Task = Tasks[I];
		for (const FTaskGraphDependency& Dep : Task.Node->Dependencies)
		{
			auto It = Lookup.find({ Dep.Name, Dep.Stage });
			if (It == Lookup.end())
			{
				return false;   // missing dependency
			}
			Tasks[It->second].Downstreams.push_back(I);
			Task.InitPending += 1;
		}
		Task.Pending = Task.InitPending;
	}

	return true;
}

void FTaskGraph::Reset()
{
	for (FTask& Task : Tasks)
	{
		Task.Pending = Task.InitPending;
	}
	Remaining = static_cast<std::uint32_t>(Tasks.size());
}

void FTaskGraph::Execute()
{
	if (Tasks.empty())
	{
		return;
	}
	Reset();

	// Seed: collect all tasks with zero pending deps.
	std::vector<std::size_t> Ready;
	for (std::size_t I = 0; I < Tasks.size(); ++I)
	{
		if (Tasks[I].Pending == 0)
		{
			Ready.push_back(I);
		}
	}

	// Dispatch loop: submit ready tasks; on completion, decrement downstreams
	// and submit any that become ready. The completion counting happens in the
	// task's runner wrapper (submitted to the pool).
	for (std::size_t Index : Ready)
	{
		SubmitTask(Index);
	}
}

void FTaskGraph::SubmitTask(std::size_t Index)
{
	Pool.Submit([this, Index]()
	{
		FTask& Task = Tasks[Index];
		ExecuteNodeFor(Index);

		// Release downstreams.
		std::vector<std::size_t> BecameReady;
		{
			std::lock_guard Lock(Mutex);
			for (std::size_t Down : Task.Downstreams)
			{
				FTask& D = Tasks[Down];
				if (D.Pending != 0 && --D.Pending == 0)
				{
					BecameReady.push_back(Down);
				}
			}
			Remaining -= 1;
		}
		for (std::size_t Down : BecameReady)
		{
			SubmitTask(Down);
		}
	});
}

void FTaskGraph::ExecuteNodeFor(std::size_t Index)
{
	ExecuteNode(Tasks[Index].Node);
}

void FTaskGraph::Flush()
{
	Pool.Flush();
}

} // namespace Maho
