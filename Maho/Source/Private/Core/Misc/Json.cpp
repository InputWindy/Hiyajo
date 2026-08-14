#include <Core/Misc/Json.h>

#include <Core/Misc/Log.h>
#include <Core/Misc/Utf8Path.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <utility>

namespace Maho
{

struct FJsonValue::FImpl
{
	nlohmann::json Data;
};

struct FJsonInternals
{
	static std::shared_ptr<FJsonValue::FImpl> MakeImpl(nlohmann::json Data)
	{
		auto Impl = std::make_shared<FJsonValue::FImpl>();
		Impl->Data = std::move(Data);
		return Impl;
	}
};

namespace
{

[[nodiscard]] EJsonType ToJsonType(const nlohmann::json& Data)
{
	if (Data.is_null())
	{
		return EJsonType::Null;
	}
	if (Data.is_boolean())
	{
		return EJsonType::Bool;
	}
	if (Data.is_number())
	{
		return EJsonType::Number;
	}
	if (Data.is_string())
	{
		return EJsonType::String;
	}
	if (Data.is_array())
	{
		return EJsonType::Array;
	}
	if (Data.is_object())
	{
		return EJsonType::Object;
	}
	return EJsonType::Null;
}

} // namespace

FJsonValue::FJsonValue()
	: Impl(FJsonInternals::MakeImpl(nullptr))
{
}

FJsonValue::FJsonValue(std::shared_ptr<FImpl> InImpl)
	: Impl(std::move(InImpl))
{
	if (!Impl)
	{
		Impl = FJsonInternals::MakeImpl(nullptr);
	}
}

FJsonValue::FJsonValue(const FJsonValue& Other)
	: Impl(Other.Impl ? FJsonInternals::MakeImpl(Other.Impl->Data) : FJsonInternals::MakeImpl(nullptr))
{
}

FJsonValue::FJsonValue(FJsonValue&& Other) noexcept
	: Impl(std::move(Other.Impl))
{
	Other.Impl = FJsonInternals::MakeImpl(nullptr);
}

FJsonValue& FJsonValue::operator=(const FJsonValue& Other)
{
	if (this != &Other)
	{
		Impl = Other.Impl ? FJsonInternals::MakeImpl(Other.Impl->Data) : FJsonInternals::MakeImpl(nullptr);
	}
	return *this;
}

FJsonValue& FJsonValue::operator=(FJsonValue&& Other) noexcept
{
	if (this != &Other)
	{
		Impl = std::move(Other.Impl);
		Other.Impl = FJsonInternals::MakeImpl(nullptr);
	}
	return *this;
}

FJsonValue::~FJsonValue() = default;

FJsonValue FJsonValue::Null()
{
	return FJsonValue(FJsonInternals::MakeImpl(nullptr));
}

FJsonValue FJsonValue::Bool(bool Value)
{
	return FJsonValue(FJsonInternals::MakeImpl(Value));
}

FJsonValue FJsonValue::Number(double Value)
{
	return FJsonValue(FJsonInternals::MakeImpl(Value));
}

FJsonValue FJsonValue::Number(int Value)
{
	return FJsonValue(FJsonInternals::MakeImpl(Value));
}

FJsonValue FJsonValue::Number(std::int64_t Value)
{
	return FJsonValue(FJsonInternals::MakeImpl(Value));
}

FJsonValue FJsonValue::String(std::string Value)
{
	return FJsonValue(FJsonInternals::MakeImpl(std::move(Value)));
}

FJsonValue FJsonValue::Array()
{
	return FJsonValue(FJsonInternals::MakeImpl(nlohmann::json::array()));
}

FJsonValue FJsonValue::Object()
{
	return FJsonValue(FJsonInternals::MakeImpl(nlohmann::json::object()));
}

EJsonType FJsonValue::GetType() const
{
	return Impl ? ToJsonType(Impl->Data) : EJsonType::Null;
}

bool FJsonValue::TryGetBool(bool& OutValue) const
{
	if (!Impl || !Impl->Data.is_boolean())
	{
		return false;
	}
	OutValue = Impl->Data.get<bool>();
	return true;
}

bool FJsonValue::TryGetInt(int& OutValue) const
{
	if (!Impl || !Impl->Data.is_number_integer())
	{
		if (Impl && Impl->Data.is_number())
		{
			OutValue = static_cast<int>(Impl->Data.get<double>());
			return true;
		}
		return false;
	}
	OutValue = Impl->Data.get<int>();
	return true;
}

bool FJsonValue::TryGetInt64(std::int64_t& OutValue) const
{
	if (!Impl || !Impl->Data.is_number())
	{
		return false;
	}
	OutValue = Impl->Data.get<std::int64_t>();
	return true;
}

bool FJsonValue::TryGetFloat(float& OutValue) const
{
	double DoubleValue = 0.0;
	if (!TryGetDouble(DoubleValue))
	{
		return false;
	}
	OutValue = static_cast<float>(DoubleValue);
	return true;
}

bool FJsonValue::TryGetDouble(double& OutValue) const
{
	if (!Impl || !Impl->Data.is_number())
	{
		return false;
	}
	OutValue = Impl->Data.get<double>();
	return true;
}

bool FJsonValue::TryGetString(std::string& OutValue) const
{
	if (!Impl || !Impl->Data.is_string())
	{
		return false;
	}
	OutValue = Impl->Data.get<std::string>();
	return true;
}

bool FJsonValue::AsBool(bool DefaultValue) const
{
	bool Value = DefaultValue;
	(void)TryGetBool(Value);
	return Value;
}

int FJsonValue::AsInt(int DefaultValue) const
{
	int Value = DefaultValue;
	(void)TryGetInt(Value);
	return Value;
}

std::int64_t FJsonValue::AsInt64(std::int64_t DefaultValue) const
{
	std::int64_t Value = DefaultValue;
	(void)TryGetInt64(Value);
	return Value;
}

float FJsonValue::AsFloat(float DefaultValue) const
{
	float Value = DefaultValue;
	(void)TryGetFloat(Value);
	return Value;
}

double FJsonValue::AsDouble(double DefaultValue) const
{
	double Value = DefaultValue;
	(void)TryGetDouble(Value);
	return Value;
}

std::string FJsonValue::AsString(const char* DefaultValue) const
{
	std::string Value;
	if (TryGetString(Value))
	{
		return Value;
	}
	return DefaultValue ? DefaultValue : "";
}

bool FJsonValue::HasField(const char* Name) const
{
	return Impl && Name && Impl->Data.is_object() && Impl->Data.contains(Name);
}

FJsonValue FJsonValue::GetField(const char* Name) const
{
	if (!HasField(Name))
	{
		return Null();
	}
	return FJsonValue(FJsonInternals::MakeImpl(Impl->Data.at(Name)));
}

void FJsonValue::SetField(const char* Name, const FJsonValue& Value)
{
	if (!Name || !Impl)
	{
		return;
	}
	if (!Impl->Data.is_object())
	{
		Impl->Data = nlohmann::json::object();
	}
	Impl->Data[Name] = Value.Impl ? Value.Impl->Data : nlohmann::json(nullptr);
}

void FJsonValue::RemoveField(const char* Name)
{
	if (!HasField(Name))
	{
		return;
	}
	Impl->Data.erase(Name);
}

std::vector<std::string> FJsonValue::GetFieldNames() const
{
	std::vector<std::string> Names;
	if (!Impl || !Impl->Data.is_object())
	{
		return Names;
	}
	Names.reserve(Impl->Data.size());
	for (auto It = Impl->Data.begin(); It != Impl->Data.end(); ++It)
	{
		Names.push_back(It.key());
	}
	return Names;
}

std::size_t FJsonValue::GetArraySize() const
{
	return (Impl && Impl->Data.is_array()) ? Impl->Data.size() : 0;
}

FJsonValue FJsonValue::GetElement(std::size_t Index) const
{
	if (!Impl || !Impl->Data.is_array() || Index >= Impl->Data.size())
	{
		return Null();
	}
	return FJsonValue(FJsonInternals::MakeImpl(Impl->Data.at(Index)));
}

void FJsonValue::SetElement(std::size_t Index, const FJsonValue& Value)
{
	if (!Impl)
	{
		return;
	}
	if (!Impl->Data.is_array())
	{
		Impl->Data = nlohmann::json::array();
	}
	while (Impl->Data.size() <= Index)
	{
		Impl->Data.push_back(nullptr);
	}
	Impl->Data[Index] = Value.Impl ? Value.Impl->Data : nlohmann::json(nullptr);
}

void FJsonValue::AddElement(const FJsonValue& Value)
{
	if (!Impl)
	{
		return;
	}
	if (!Impl->Data.is_array())
	{
		Impl->Data = nlohmann::json::array();
	}
	Impl->Data.push_back(Value.Impl ? Value.Impl->Data : nlohmann::json(nullptr));
}

void FJsonValue::ClearArray()
{
	if (!Impl)
	{
		return;
	}
	Impl->Data = nlohmann::json::array();
}

FJsonDocument::FJsonDocument() = default;

FJsonDocument::~FJsonDocument() = default;

FJsonDocument::FJsonDocument(FJsonDocument&&) noexcept = default;

FJsonDocument& FJsonDocument::operator=(FJsonDocument&&) noexcept = default;

bool FJsonDocument::Parse(const std::string& Text)
{
	LastError.clear();
	try
	{
		nlohmann::json Parsed = nlohmann::json::parse(Text);
		Root = FJsonValue(FJsonInternals::MakeImpl(std::move(Parsed)));
		return true;
	}
	catch (const std::exception& Exception)
	{
		LastError = Exception.what();
		Root = FJsonValue::Null();
		MAHO_CORE_ERROR("FJsonDocument::Parse failed: {}", LastError);
		return false;
	}
}

bool FJsonDocument::LoadFromFile(const std::string& FilePath)
{
	SourcePath = FilePath;
	LastError.clear();

	std::ifstream Input(PathFromUtf8(FilePath), std::ios::binary);
	if (!Input)
	{
		LastError = "failed to open file";
		Root = FJsonValue::Null();
		return false;
	}

	std::ostringstream Buffer;
	Buffer << Input.rdbuf();
	return Parse(Buffer.str());
}

bool FJsonDocument::SaveToFile(const std::string& FilePath, bool bPretty) const
{
	std::ofstream Output(PathFromUtf8(FilePath), std::ios::binary | std::ios::trunc);
	if (!Output)
	{
		MAHO_CORE_ERROR("FJsonDocument::SaveToFile failed to open '{}'", FilePath);
		return false;
	}

	Output << Stringify(bPretty);
	return static_cast<bool>(Output);
}

std::string FJsonDocument::Stringify(bool bPretty) const
{
	if (!Root.Impl)
	{
		return bPretty ? "null\n" : "null";
	}

	if (bPretty)
	{
		return Root.Impl->Data.dump(2);
	}
	return Root.Impl->Data.dump();
}

void FJsonDocument::SetRoot(FJsonValue Value)
{
	Root = std::move(Value);
}

} // namespace Maho
