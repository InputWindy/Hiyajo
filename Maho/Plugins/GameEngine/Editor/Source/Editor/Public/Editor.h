#pragma once

#include "EditorApi.h"
#include <Core/Core.h>

namespace Maho
{

namespace Editor
{

	/** Editor tooling extension (ImGui editor UI). Engine extension (driven by EEngineStage). */
	class MAHO_EDITOR_API FEditorSystem : public TExtension<EEngineStage, FEditorSystem>
	{
	public:
		[[nodiscard]] bool ExecuteStage(EEngineStage Stage) override;

	private:
		friend TSingleton<FEditorSystem>;
		FEditorSystem() = default;
	};

} // namespace Editor

} // namespace Maho
