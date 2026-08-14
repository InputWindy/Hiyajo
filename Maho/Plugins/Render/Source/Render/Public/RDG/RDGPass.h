#pragma once

#include "RenderApi.h"
#include <RHI/RHICommandList.h>
#include <RHI/RHIEnums.h>
#include <RHI/RHIResources.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace Maho
{

class FRDGResource;
class FRDGTexture;
struct FRDGPassParameters;

enum class ERDGPassType : uint8_t
{
	Raster,
	Compute,
	Copy
};

struct FRDGResourceAccess
{
	FRDGResource* Resource = nullptr;
	ERHIResourceState RequiredState = ERHIResourceState::Common;
};

class MAHO_RENDER_API FRDGPass
{
public:
	using FExecuteFunc = std::function<void(FRHICommandList&)>;

	FRDGPass(const char* Name, ERDGPassType Type);
	FRDGPass(const FRDGPass&) = delete;
	FRDGPass& operator=(const FRDGPass&) = delete;
	~FRDGPass() = default;

	[[nodiscard]] const char* GetName() const { return Name; }
	[[nodiscard]] ERDGPassType GetType() const { return Type; }

	void AddRead(FRDGResource* Resource, ERHIResourceState State);
	void AddWrite(FRDGResource* Resource, ERHIResourceState State);
	[[nodiscard]] const std::vector<FRDGResourceAccess>& GetReads() const { return Reads; }
	[[nodiscard]] const std::vector<FRDGResourceAccess>& GetWrites() const { return Writes; }

	void SetExecute(FExecuteFunc InExecute) { Execute = std::move(InExecute); }
	[[nodiscard]] FExecuteFunc& GetExecute() { return Execute; }

	void SetParameters(const FRDGPassParameters* InParams) { Parameters = InParams; }
	[[nodiscard]] const FRDGPassParameters* GetParameters() const { return Parameters; }

private:
	friend class FRDGBuilder;

	const char* Name = nullptr;
	ERDGPassType Type = ERDGPassType::Raster;
	std::vector<FRDGResourceAccess> Reads;
	std::vector<FRDGResourceAccess> Writes;
	FExecuteFunc Execute;
	const FRDGPassParameters* Parameters = nullptr;
};

} // namespace Maho
