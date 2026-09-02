# Asset — API 文档

服务层：`FAssetRegistry` 是 `FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>`（`Asset.dll`）。它把内容目录里的磁盘资源（`Content/Materials/M_Metal.material`）索引成逻辑路径 `/Game/Materials/M_Metal` 并附带元数据 `FAssetData`；`Find` 按逻辑路径查元数据，`Resolve` 经 `FPaths` 根别名映射回物理文件，`Load` 读原始字节。零资源格式假设——不理解材质/纹理内容，只做索引与 I/O。

## Asset.h

### EAssetType <enum class>

资源类型枚举——可扩展，当前由磁盘扩展名推断（见实现字典 `TypeFromExtension`）。默认 `Unknown`。

#### 枚举值

| 枚举 | 说明 |
|------|------|
| `Unknown = 0` | 未识别扩展名 / 未设置 |
| `Material` | 材质（`.material`） |
| `Texture` | 纹理（`.texture`） |

### FAssetPath <class>

逻辑资源路径——**无扩展名、无对象部分**（引擎没有 UObject 体系）。相等/序比较基于内部字符串。

#### 接口

| 签名 | 说明 |
|------|------|
| `FAssetPath() = default` | 空路径 |
| `explicit FAssetPath(std::string InPath)` | 从字符串构造（`"/Game/Materials/M_Metal"`） |
| `std::string_view GetPath() const` | 底层路径字符串 |
| `bool operator==(const FAssetPath&) const` | 路径相等（字符串相等） |
| `bool operator!=(const FAssetPath&) const` | 路径不等 |
| `bool operator<(const FAssetPath&) const` | 按字符串序比较（可作 `std::map` 键） |

### FAssetData <struct>

已解析的资源元数据——`Scan` 时填充。

#### 成员变量

| 字段 | 类型 | 说明 |
|------|------|------|
| `Path` | `FAssetPath` | 逻辑路径（`/Game/Materials/M_Metal`） |
| `Type` | `EAssetType` | 资源类型（默认 `Unknown`） |
| `File` | `std::filesystem::path` | 物理文件（`Content/Materials/M_Metal.material`） |
| `Dependencies` | `std::vector<FAssetPath>` | 依赖列表——反序列化时懒填充，`Scan` 不填 |

### GetAssetRegistry <自由函数>

全局资源注册表访问器——**跨 DLL 经函数而非裸变量**。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_ASSET_API FAssetRegistry* GetAssetRegistry()` | 返回已初始化的 `FAssetRegistry*`；`Initialize` 前 / `Shutdown` 后为 `nullptr` |

### FAssetRegistry <class>

资源注册表：逻辑路径 ↔ 元数据。`Scan` 递归索引内容目录，`Find` 查逻辑路径，`Resolve` 映射物理文件，`Load` 读原始字节。内部 `Assets`（`std::map<std::string, FAssetData>`）由 `Mutex` 全程保护，读取线程安全。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_DECLARE_LAYER(FAssetRegistry, "Asset.dll")` | 层声明宏（DLL 导出入口） |
| `void Scan(const std::filesystem::path& ContentDir, std::string_view MountAlias = "Game")` | 递归索引内容目录；把 `MountAlias` 注册为 `FPaths` 根；`Content/Materials/M_Metal.material` → `/Game/Materials/M_Metal` (Material) |
| `const FAssetData* Find(const FAssetPath& Path) const` | 按逻辑路径查元数据；缺失返回 `nullptr` |
| `std::filesystem::path Resolve(const FAssetPath& Path) const` | 逻辑路径 → 物理文件（剥掉前导 `/` 后委托 `FPaths::Resolve`，mount 别名是根） |
| `std::optional<std::vector<std::uint8_t>> Load(const FAssetPath& Path) const` | 读资源文件原始字节；缺失 / 不可读返回 `nullopt` |
| `std::size_t GetAssetCount() const` | 已索引资源数 |

## AssetApi.h

### MAHO_ASSET_API <宏>

DLL 导出/导入宏——`MAHO_ASSET_MODULE_EXPORTS` 定义时展开为 `MAHO_EXPORT`，否则为 `MAHO_IMPORT`（详见 `Core/Export.h`）。

#### 宏

| 签名 | 说明 |
|------|------|
| `MAHO_ASSET_API` | 修饰本 DLL 导出的符号（`GetAssetRegistry`、`CreateLayer`） |

- [Asset.md](Asset.md) — 概念 · [实现字典](ImplAPI.md) — 算法
