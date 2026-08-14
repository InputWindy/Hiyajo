#pragma once

#include <Core/Misc/Export.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Maho
{

enum class EJsonType : std::uint8_t
{
	Null = 0,
	Bool,
	Number,
	String,
	Array,
	Object,
};

/**
 * JSON value (object / array / scalar). Deep-copyable; backed privately by nlohmann/json.
 *
 * Example:
 * ```
 *   Maho::FJsonValue Root = Maho::FJsonValue::Object();
 *   Root.SetField("name", Maho::FJsonValue::String("Hero"));
 *   Root.SetField("hp", Maho::FJsonValue::Number(100));
 *
 *   Maho::FJsonValue Skills = Maho::FJsonValue::Array();
 *   Skills.AddElement(Maho::FJsonValue::String("Slash"));
 *   Root.SetField("skills", Skills);
 *
 *   int Hp = 0;
 *   Root.GetField("hp").TryGetInt(Hp);
 * ```
 */
class MAHO_API FJsonValue
{
public:
	FJsonValue();
	FJsonValue(const FJsonValue& Other);
	FJsonValue(FJsonValue&& Other) noexcept;
	FJsonValue& operator=(const FJsonValue& Other);
	FJsonValue& operator=(FJsonValue&& Other) noexcept;
	~FJsonValue();

	[[nodiscard]] static FJsonValue Null();
	[[nodiscard]] static FJsonValue Bool(bool Value);
	[[nodiscard]] static FJsonValue Number(double Value);
	[[nodiscard]] static FJsonValue Number(int Value);
	[[nodiscard]] static FJsonValue Number(std::int64_t Value);
	[[nodiscard]] static FJsonValue String(std::string Value);
	[[nodiscard]] static FJsonValue Array();
	[[nodiscard]] static FJsonValue Object();

	[[nodiscard]] EJsonType GetType() const;
	[[nodiscard]] bool IsNull() const { return GetType() == EJsonType::Null; }
	[[nodiscard]] bool IsBool() const { return GetType() == EJsonType::Bool; }
	[[nodiscard]] bool IsNumber() const { return GetType() == EJsonType::Number; }
	[[nodiscard]] bool IsString() const { return GetType() == EJsonType::String; }
	[[nodiscard]] bool IsArray() const { return GetType() == EJsonType::Array; }
	[[nodiscard]] bool IsObject() const { return GetType() == EJsonType::Object; }

	[[nodiscard]] bool TryGetBool(bool& OutValue) const;
	[[nodiscard]] bool TryGetInt(int& OutValue) const;
	[[nodiscard]] bool TryGetInt64(std::int64_t& OutValue) const;
	[[nodiscard]] bool TryGetFloat(float& OutValue) const;
	[[nodiscard]] bool TryGetDouble(double& OutValue) const;
	[[nodiscard]] bool TryGetString(std::string& OutValue) const;

	[[nodiscard]] bool AsBool(bool DefaultValue = false) const;
	[[nodiscard]] int AsInt(int DefaultValue = 0) const;
	[[nodiscard]] std::int64_t AsInt64(std::int64_t DefaultValue = 0) const;
	[[nodiscard]] float AsFloat(float DefaultValue = 0.0f) const;
	[[nodiscard]] double AsDouble(double DefaultValue = 0.0) const;
	[[nodiscard]] std::string AsString(const char* DefaultValue = "") const;

	/** Object helpers (no-op / null if not an object). */
	[[nodiscard]] bool HasField(const char* Name) const;
	[[nodiscard]] bool HasField(const std::string& Name) const { return HasField(Name.c_str()); }
	[[nodiscard]] FJsonValue GetField(const char* Name) const;
	[[nodiscard]] FJsonValue GetField(const std::string& Name) const { return GetField(Name.c_str()); }
	void SetField(const char* Name, const FJsonValue& Value);
	void SetField(const std::string& Name, const FJsonValue& Value) { SetField(Name.c_str(), Value); }
	void RemoveField(const char* Name);
	[[nodiscard]] std::vector<std::string> GetFieldNames() const;

	/** Array helpers (no-op / null if not an array). */
	[[nodiscard]] std::size_t GetArraySize() const;
	[[nodiscard]] FJsonValue GetElement(std::size_t Index) const;
	void SetElement(std::size_t Index, const FJsonValue& Value);
	void AddElement(const FJsonValue& Value);
	void ClearArray();

private:
	friend class FJsonDocument;
	friend struct FJsonInternals;

	struct FImpl;
	explicit FJsonValue(std::shared_ptr<FImpl> InImpl);

	std::shared_ptr<FImpl> Impl;
};

/**
 * JSON document reader / writer (file + string).
 * Root defaults to a null value until Parse / Load / SetRoot.
 *
 * Example:
 * ```
 *   Maho::FJsonDocument Doc;
 *   if (Doc.LoadFromFile("Cached/Save.json"))
 *   {
 *       const int Level = Doc.GetRoot().GetField("level").AsInt(1);
 *   }
 *
 *   Maho::FJsonValue Root = Maho::FJsonValue::Object();
 *   Root.SetField("level", Maho::FJsonValue::Number(3));
 *   Doc.SetRoot(Root);
 *   Doc.SaveToFile("Cached/Save.json", true);
 * ```
 */
class MAHO_API FJsonDocument
{
public:
	FJsonDocument();
	~FJsonDocument();

	FJsonDocument(const FJsonDocument&) = delete;
	FJsonDocument& operator=(const FJsonDocument&) = delete;
	FJsonDocument(FJsonDocument&&) noexcept;
	FJsonDocument& operator=(FJsonDocument&&) noexcept;

	[[nodiscard]] bool Parse(const std::string& Text);
	[[nodiscard]] bool LoadFromFile(const std::string& FilePath);
	[[nodiscard]] bool SaveToFile(const std::string& FilePath, bool bPretty = true) const;

	[[nodiscard]] std::string Stringify(bool bPretty = true) const;

	[[nodiscard]] const FJsonValue& GetRoot() const { return Root; }
	[[nodiscard]] FJsonValue& GetRoot() { return Root; }
	void SetRoot(FJsonValue Value);

	[[nodiscard]] const std::string& GetLastError() const { return LastError; }
	[[nodiscard]] const std::string& GetSourcePath() const { return SourcePath; }

private:
	FJsonValue Root;
	std::string LastError;
	std::string SourcePath;
};

} // namespace Maho
