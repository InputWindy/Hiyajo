#include <Core/TaskGraph.h>

#include <Core/Fatal.h>

#include <functional>
#include <utility>

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
		auto Task = std::make_unique<FTask>();
		Task->Node = Node;
		Tasks.push_back(std::move(Task));
		Lookup[{ Node->Name, Node->Stage }] = Index;
	}
}

bool FTaskGraph::Compile()
{
	CompileErrorNode.clear();
	if (Tasks.empty())
	{
		return true;
	}

	// Reset per-task graph state.
	for (const auto& Task : Tasks)
	{
		Task->Downstreams.clear();
		Task->InitPending = 0;
		Task->Pending.store(0);
	}

	// Pass 1: wire edges + missing-dependency validation.
	for (std::size_t I = 0; I < Tasks.size(); ++I)
	{
		FTask& Task = *Tasks[I];
		for (const FTaskGraphDependency& Dep : Task.Node->Dependencies)
		{
			auto It = Lookup.find({ Dep.Name, Dep.Stage });
			if (It == Lookup.end())
			{
				CompileErrorNode = Task.Node->Name;   // the layer with the bad dep
				return false;                          // missing dependency
			}
			Tasks[It->second]->Downstreams.push_back(I);
			Task.InitPending += 1;
		}
		Task.Pending.store(Task.InitPending);
	}

	// Pass 2: cycle detection (iterative DFS; a back-edge = cycle). A cycle has
	// no ready node and would otherwise hang Flush() forever. Iterative with an
	// explicit stack so a deep dependency chain cannot overflow the call stack.
	{
		std::vector<int> Color(Tasks.size(), 0);   // 0 = white, 1 = gray (in-stack), 2 = black
		std::vector<std::pair<std::size_t, std::size_t>> Stack;   // (task, next-dep-index)
		for (std::size_t V = 0; V < Tasks.size(); ++V)
		{
			if (Color[V] != 0)
			{
				continue;
			}
			Stack.emplace_back(V, 0);
			Color[V] = 1;
			while (!Stack.empty())
			{
				const std::size_t Cur = Stack.back().first;
				const auto& Deps = Tasks[Cur]->Node->Dependencies;
				if (Stack.back().second >= Deps.size())
				{
					Color[Cur] = 2;
					Stack.pop_back();
					continue;
				}
				const FTaskGraphDependency& Dep = Deps[Stack.back().second++];
				auto It = Lookup.find({ Dep.Name, Dep.Stage });
				if (It == Lookup.end())
				{
					continue;   // missing dep already reported in pass 1
				}
				const std::size_t U = It->second;
				if (Color[U] == 1)
				{
					CompileErrorNode = Tasks[Cur]->Node->Name;   // Cur is in the cycle
					break;
				}
				if (Color[U] == 0)
				{
					Stack.emplace_back(U, 0);
					Color[U] = 1;
				}
			}
			if (!CompileErrorNode.empty())
			{
				break;
			}
		}
		if (!CompileErrorNode.empty())
		{
			return false;   // cycle
		}
	}

	return true;
}

void FTaskGraph::Reset()
{
	for (const auto& Task : Tasks)
	{
		Task->Pending.store(Task->InitPending, std::memory_order_relaxed);
	}
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
		if (Tasks[I]->Pending.load(std::memory_order_relaxed) == 0)
		{
			Ready.push_back(I);
		}
	}

	for (std::size_t Index : Ready)
	{
		SubmitTask(Index);
	}
}

void FTaskGraph::SubmitTask(std::size_t Index)
{
	Pool.Submit([this, Index]()
	{
		FTask& T = *Tasks[Index];

		// Execute the node. A throwing stage method must not kill the host: report
		// it (non-fatal) and still release downstreams so the graph never hangs.
		try
		{
			ExecuteNodeFor(Index);
		}
		catch (const std::exception& E)
		{
			ReportError((std::string("layer stage threw: ") + E.what()
				+ " (node " + T.Node->Name + ")").c_str());
		}
		catch (...)
		{
			ReportError((std::string("layer stage threw unknown exception (node ")
				+ T.Node->Name + ")").c_str());
		}

		// Lock-free release: each predecessor fetch_sub's the downstream; the
		// LAST one (old value == 1) is the only one that submits it.
		std::vector<std::size_t> BecameReady;
		for (std::size_t Down : T.Downstreams)
		{
			if (Tasks[Down]->Pending.fetch_sub(1, std::memory_order_acq_rel) == 1)
			{
				BecameReady.push_back(Down);
			}
		}
		for (std::size_t Down : BecameReady)
		{
			SubmitTask(Down);
		}
	});
}

void FTaskGraph::ExecuteNodeFor(std::size_t Index)
{
	ExecuteNode(Tasks[Index]->Node);
}

void FTaskGraph::Flush()
{
	Pool.Flush();
}

} // namespace Maho
