#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Asset 插件的文档内容。

运行器（Tools/plugin_docs.py）把 docs_builder 作为变量 D 注入本文件 —— 只声明，不 import。
"""

# ══════════════════════════════════════════════════════════════════════════════
# Private/TextureImageCodec.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Private/TextureImageCodec.h",
         Title="TextureImageCodec.h —— CPU 图像解码（插件私有）",
         Desc="把磁盘字节解成 CPU 像素。它放在 **Private/** 而不是 Public/，因为它是纯实现细节："
              "只有本插件自己的贴图 importer 调它，外部（含项目侧）永远拿不到「解码器」这个符号 —— "
              "于是换掉 WIC、以后加 KTX2 都不算接口变更。\n"
              "入口刻意是 `DecodeFromMemory`（内存 + 字节数）而不是「给个路径」：读取磁盘是 Resource "
              "那条 IO 线程的职责，解码只认字节。路径只当**格式提示**（按扩展名嗅探），"
              "所以同一个解码器可以喂给任何来源的字节。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("AssetTypes.h", "解码结果 `FDecodedImage` 的字段（维度 / 像素格式 / bSRGB）直接复用资产侧那套枚举 —— "
                      "避免在解码器和资产类型之间维护两份会各自漂移的定义")
D.Row("<cstddef> / <cstdint>", "`std::size_t` 字节数与定宽整型（像素字段要与磁盘布局逐位一致）")
D.Row("<string> / <string_view>", "返回小写扩展名；入参路径用 `string_view` 以免为格式提示复制一份字符串")
D.Row("<vector>", "解码后的像素缓冲")

D.Card("自由函数（namespace TextureImageCodec）")
D.Table("签名", "说明")
D.Row("bool DecodeFromMemory(const std::uint8_t* Bytes, std::size_t ByteCount, "
      "std::string_view SourcePath, FDecodedImage& Out)",
      "把内存里的栅格图（PNG/JPG…）解成 RGBA8 写入 `Out`。成功才返回 true；失败时 `Out` 不保证可用。"
      "`SourcePath` 只用于扩展名嗅探（决定走哪条编解码路径），不读盘 —— 这也是它能被别的来源"
      "（打包后的字节、测试里的内存数据）复用的原因。")
D.Row("std::string GetExtensionLower(std::string_view Path)",
      "取小写扩展名（含点，如 `.png`）；没有扩展名返回空串。单独拎出来是因为「按扩展名分流」"
      "在解码与解码之外都要用，集中一处就只有一个大小写处理口径")
D.Row("bool IsRasterExtension(std::string_view Ext)",
      "该扩展名是否属于 WIC 能解的栅格格式。importer 用它决定「交给图像解码」还是「当作原生容器拒绝」，"
      "使格式清单只此一份")

D.Struct("FDecodedImage", Desc="一次解码的 CPU 图像载荷。它是个**中转记录**：importer 把它的字段抄进 "
                               "FTexture 的 payload 构造函数，抄完即弃 —— 所以它不需要虚函数、不需要导出标记，"
                               "也不可能被跨 DLL 持有。")
D.SetAccess("public")
D.Field("ETextureDimension Dimension = ETextureDimension::Tex2D", "维度/2D/3D/Cube/数组）")
D.Field("ETexturePixelFormat Format = ETexturePixelFormat::RGBA8", "解码后的像素格式（当前恒为 RGBA8）")
D.Field("std::uint32_t Width = 0", "宽（像素）")
D.Field("std::uint32_t Height = 0", "高（像素）")
D.Field("std::uint32_t Depth = 1", "3D 贴图深度；其余维度恒为 1")
D.Field("std::uint32_t ArrayLayers = 1", "数组层数 / Cube 的 6 面")
D.Field("std::uint32_t MipCount = 1", "mip 级数")
D.Field("bool bSRGB = true", "像素是否按 sRGB 解释（决定 GPU 侧采样是否做线性化）")
D.Field("std::vector<std::uint8_t> Pixels", "解码后的像素字节（RGBA8，行优先）")

# ══════════════════════════════════════════════════════════════════════════════
# Public/AssetApi.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/AssetApi.h", Title="AssetApi.h —— 插件导出标记",
         Desc="这个头只有一条内容：导出标记 `MAHO_ASSET_API`。它存在的理由和资产类型本身一样 —— "
              "`FAssetsResource` 及其子类的实例在 Asset.dll 里构造，却由 Resource 的目录销毁，"
              "而触发导入的调用方可能是**早于目录被卸载的子插件**。把类型标成 DLL 接口后，每个消费者都必须"
              "通过导入表引用该类型的 vftable 与删除析构，编译器就无法把「即将被释放的模块」的 vptr 烙进实例。"
              "拿掉这个标记，失败不是编译错误，而是 `delete` 里的静默 0xC0000005。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT` 原语。引擎 core 是插件唯一的下层依赖方向，"
                       "本插件不 include 任何上层头")

D.Macro("MAHO_ASSET_API", "MAHO_EXPORT / MAHO_IMPORT",
        "构建 Asset.dll 时（`MAHO_ASSET_MODULE_EXPORTS` 由本插件的构建定义）取 `MAHO_EXPORT`，"
        "其余消费者取 `MAHO_IMPORT` —— 同一份头在「我导出」与「我导入」两侧各自成立，"
        "无需维护两套声明，也不会有一份 header-inline 的副本被编进每个 DLL。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/AssetTypes.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/AssetTypes.h", Title="AssetTypes.h —— 具体资产类型（casset 容器 + 各 codec）",
         Desc="Resource 插件只认识 `FResource` 基类、`FResourceSystem` 框架（IO 线程 + FName 目录）"
              "以及三个模板钩子（`TResourceImporter` / `TResourceExporter` / `TResourceCreator`）；"
              "**具体的资产类型全部落在本插件**：枚举（资产类型 / 像素格式 / 采样参数）、casset 容器"
              "`FAssetsResource`、以及贴图 / 静态网格 / 骨骼 / 动画 / 材质 / 文档型资产的类。\n"
              "分层的意图是让 Resource 保持类型无关（它只搬运字节），而「资产是什么」这件事只在这里定义。"
              "容器层与业务层的写入顺序也由此固定：子类 `Serialize` **先**调基类写容器头"
              "（magic + version + type + 依赖表），**再**追加自身字段 —— 顺序不能换，读回时容器头是"
              "定位业务字段的唯一起点。\n"
              "**每个具体类型都带 `MAHO_ASSET_API`**：实例在 Asset.dll 里构造、由目录销毁，"
              "触发导入的调用方可能先被卸载。导出标记把 vtable 与删除析构钉在本模块，"
              "使任何消费者都无法把自己（即将释放）的 vptr 烙进实例。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("AssetApi.h", "本插件的导出标记 `MAHO_ASSET_API`（决定下面每个类型是导出还是导入）")
D.Row("Resource.h", "`FResource` 基类、`FResourceSystem`、以及三个要特化的模板钩子")
D.Row("<Archive.h>", "`Archive::ISerialize` / `FArchive`：casset 的序列化接口与字节流")
D.Row("<cstdint> / <span>", "枚举底层类型；`PeekCassetAssetType` 以 `std::span<const std::uint8_t>` "
                            "接收「还不确定是不是容器」的字节（只读、不拷贝、不做所有权假设）")
D.Row("<string> / <utility> / <vector>", "路径与字段名；`std::move` 转发构造参数；像素 / 顶点等载荷缓冲")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("EAssetType PeekCassetAssetType(std::span<const std::uint8_t> Bytes)",
      "只读固定容器头，返回其中记录的资产类型；字节不是可识别的容器（magic 不对 / 太短 / "
      "版本不支持）时返回 `EAssetType::Unknown`。**不解码、不分配、不抛异常** —— 目的就是让调用方"
      "能在把文件交给资源系统之前挑对 `TResourceImporter<T>`。不认识的类型字节原样返回，"
      "由调用方判定为不支持。")

D.Card("模板特化：TResourceCreator<T> —— 实例工厂（定义在类型自己的模块里）")
D.Table("特化", "说明")
D.Row("TResourceCreator<FTexture1D / FTexture2D / FTexture3D / FTextureCube / FTexture2DArray / "
      "FTextureCubeArray>",
      "`Create(std::string_view Path)`：out-of-line 在 AssetTypes.cpp（Asset.dll）里构造实例")
D.Row("TResourceCreator<FTexture2D>（第二重载）",
      "`Create(std::string_view Path, const TResourceCreateDesc<FTexture2D>::FConfig& Config)`："
      "运行期建资源的那条路，用描述符填好维度 / 格式 / 采样参数")
D.Row("TResourceCreator<FMaterial / FStaticMesh / FSkeleton / FAnimation / FAnimationGraph / FPrefab>",
      "`Create(std::string_view Path)`：同上。**这里必须 out-of-line 定义、绝不能 inline 在头里**，"
      "否则 `Import<T>` / `CreateResource<T>` 会在调用方模块里实例化，vtable 又落回调用方")

D.Card("模板特化：TResourceImporter<T> / TResourceExporter<T>（codec 只搬字节）")
D.Table("特化", "说明")
D.Row("TResourceImporter<FTexture1D / 2D / 3D / Cube / 2DArray / CubeArray>",
      "`Import(const FImportConfig&, std::span<const std::uint8_t>, T&, FResourceSystem&)`："
      "所有维度共用一条解码路径（维度只是 payload 字段），各自转发到同一个私有 helper；"
      "函数体在私有 .cpp 里")
D.Row("TResourceExporter<贴图六类>", "`Export(const FExportConfig&, const T&, std::vector<std::uint8_t>&)`："
                                    "把 CPU 像素编码回磁盘字节")
D.Row("TResourceImporter / TResourceExporter<FMaterial / FStaticMesh / FSkeleton / FAnimation / "
      "FAnimationGraph / FPrefab>",
      "紧凑二进制往返（这些类型没有独立的外部文件格式，codec 与字段布局一一对应）")
D.Row("`using FConfig = FImportConfig;`（importer）/ `FExportConfig`（exporter）",
      "每个特化都要暴露 FConfig —— 因为 `FResourceSystem::Import<T>` 的形参就是 "
      "`TResourceImporter<T>::FConfig`，它必须在调用点可见")

D.Enum("EAssetType", Base="std::uint8_t", Desc="资产类型，从磁盘扩展名推断并记录进 casset 头。"
                                               "留一个 `UserBase` 给项目侧扩展 —— 引擎不预设某个具体编号，"
                                               "项目类型从它往上排，就不会和引擎类型撞号。")
D.Field("Unknown = 0", "未知 / 无法识别（`PeekCassetAssetType` 的失败返回值）")
D.Field("Material", "材质")
D.Field("Texture", "贴图")
D.Field("StaticMesh", "静态网格")
D.Field("Skeleton", "骨骼")
D.Field("Animation", "动画")
D.Field("AnimationGraph", "动画图")
D.Field("Prefab", "预制体")
D.Field("UserBase", "用户自定义类型的起点：项目类型从这里往上扩展，引擎永不占用")

D.Enum("ETexturePixelFormat", Base="std::uint8_t", Desc="像素格式。贴图侧的概念（不是 RHI 枚举），"
                                                        "由 FRender 在建设 GPU 镜像时映射到 RHI 格式 —— "
                                                        "资产层因此不必 include 任何图形后端头。")
D.Field("Unknown = 0", "未知（默认值，importer 必须覆盖它）")
D.Field("RGBA8", "8 位四通道（解码路径的默认输出）")
D.Field("RGBA16F / RGBA32F", "半精度 / 单精度浮点四通道")
D.Field("R8 / RG8 / RGB8", "单 / 双 / 三通道 8 位")
D.Field("BlockCompressed", "块压缩（具体 BC 格式见后几项）")
D.Field("R16F", "半精度单通道")
D.Field("DXT1 / DXT5 / BC7", "常见块压缩格式")
D.Field("D32Sfloat", "单通道深度")
D.Field("Count", "枚举计数哨兵（便于校验与遍历）")

D.Enum("ETextureDimension", Base="std::uint8_t", Desc="贴图维度。Cube 与 CubeArray 各有一个短别名"
                                                     "（`Cube` / `CubeArray`），因为调用点两种叫法都自然。")
D.Field("Unknown = 0", "未知")
D.Field("Tex1D", "一维")
D.Field("Tex2D", "二维（默认）")
D.Field("Tex3D", "三维")
D.Field("TexCube", "立方体贴图（6 面）")
D.Field("Cube = TexCube", "`TexCube` 的别名")
D.Field("TexCubeArray", "立方体贴图数组")
D.Field("Tex2DArray", "二维数组")
D.Field("CubeArray = TexCubeArray", "`TexCubeArray` 的别名")
D.Field("Count", "枚举计数哨兵（便于校验与遍历）")

D.Enum("ETextureMirrorUsage", Base="std::uint8_t",
       Desc="以 `CreateResource` 路径**程序化创建**的贴图，其 GPU 镜像的用途：FRender 据此设置"
            "Persistent 镜像的 RHI usage。资产层抽象 —— FRender 负责映射到 `ERHITextureUsage`。")
D.Field("Sampled = 0", "普通采样贴图（默认）")
D.Field("ColorTarget", "作为颜色渲染目标（镜像带 attachment usage）")
D.Field("DepthTarget", "作为深度渲染目标")

D.Enum("ETextureSamplerMode", Base="std::uint8_t",
       Desc="采样过滤模式（GPU sampler 状态）。资产层抽象，FRender 映射到 `ERHIFilter`。")
D.Field("Nearest = 0", "最近邻（像素风 / 数据贴图）")
D.Field("Linear", "线性（默认，平滑）")

D.Enum("ETextureAddressMode", Base="std::uint8_t",
       Desc="各轴环绕模式（GPU sampler 状态）。资产层抽象，FRender 映射到 `ERHIAddressMode`。"
            "U / V / W 三轴各存一份，所以平铺与夹取可以逐轴不同。")
D.Field("Repeat = 0", "重复平铺（默认）")
D.Field("MirroredRepeat", "镜像重复")
D.Field("ClampToEdge", "夹取到边界（UI / 图集常用）")
D.Field("ClampToBorder", "夹取到边界颜色")

D.Struct("FAnimationKey", Desc="动画关键帧（值语义的小记录）。时间 + TRS 四元数/平移/缩放，"
                               "按 Hiyajo 参考形态逐字段对齐 —— 编码布局与它一一对应。")
D.SetAccess("public")
D.Field("float Time = 0.f", "关键帧时间（秒）")
D.Field("float Translation[3] = {0, 0, 0}", "平移")
D.Field("float Rotation[4] = {0, 0, 0, 1}", "旋转四元数，xyzw 顺序（注意不是常见文档的 wxyz）")
D.Field("float Scale[3] = {1, 1, 1}", "缩放")

D.Struct("FSkeletonBone", Desc="骨骼记录：名字 + 父索引 + 绑定姿态的局部矩阵。"
                               "父索引用 `-1` 表示根（不是 0，因为 0 是合法骨骼下标）。")
D.SetAccess("public")
D.Field("std::string Name", "骨骼名（动画轨道按名字绑定，因此重命名骨骼不会破坏编号）")
D.Field("std::int32_t ParentIndex = -1", "父骨骼下标；`-1` = 根")
D.Field("float BindLocal[16] = {单位矩阵}", "绑定姿态的局部矩阵（列主序 4x4，行内直接展开）")

D.Struct("FAnimationTrack", Desc="一条轨道 = 目标骨骼名 + 该骨骼的关键帧序列。"
                                 "先按名字再按索引绑定，是文档型动画的常见做法。")
D.SetAccess("public")
D.Field("std::string TargetBoneName", "目标骨骼名")
D.Field("std::vector<FAnimationKey> Keys", "关键帧（按 Time 递增）")

D.Class("FAssetsResource", Base="FResource + Archive::ISerialize",
        Desc="casset 容器资源：磁盘上一个 casset 文件对应一个实例，持有容器头（type / version）"
             "与依赖表（引用表），并实现容器层 codec。**它是所有具体资产的共同基类**，"
             "把「容器怎么读写」这件事只声明一次；子类 `Serialize` 先调它写容器头，再追加业务字段。"
             "ResourceManager 只把磁盘字节读进 bulkdata 交给 importer，不关心具体引擎类型。")
D.SetAccess("public")
D.Interface("FAssetsResource(std::string InPath, EAssetType InType)",
            "以路径 + 资产类型构造（类型会被写进容器头）")
D.Interface("[[nodiscard]] EAssetType GetAssetType() const", "读容器头记录的资产类型")
D.Interface("[[nodiscard]] std::uint32_t GetCassetVersion() const", "读容器格式版本（读回时用于兼容分支）")
D.Interface("[[nodiscard]] const std::vector<std::string>& GetDependencies() const", "读依赖表（引用到的其它资产路径）")
D.Interface("void SetDependencies(std::vector<std::string> In)", "整体写依赖表（引用表由导出方一次算好）")
D.Interface("void Serialize(Archive::FArchive& Ar) override",
            "容器层 codec：magic + version + type + 依赖表。**不写任何业务字段** —— 业务字段是子类的责任")
D.Interface("FAssetsResource(const FAssetsResource&) / operator= —— 隐式删除",
            "基类 `FResource` 只声明了移动而不声明拷贝，派生出的拷贝构造 / 拷贝赋值随之隐式删除。"
            "资源被 `unique_ptr` 持有，本来也不该按值复制（像素 / 顶点载荷复制代价高，且目录靠唯一性寻址）")
D.SetAccess("protected")
D.Field("EAssetType Type", "资产类型（写进容器头）")
D.Field("std::uint32_t Version = 1", "容器格式版本（字段布局变更时递增，读回据此分支）")
D.Field("std::vector<std::string> Dependencies", "依赖表：本资产引用到的其它资产路径")

D.Class("FTexture", Base="FAssetsResource",
        Desc="CPU 像素容器（**不是** GPU 句柄）。所有贴图维度类型共用它，维度只是它内部的一个字段；"
             "各维度再派生出自己的类型，以便 `Import<T>` 有类型可依。"
             "它同时承载 GPU 镜像的构建参数（usage / 滤波 / 环绕 / lod bias）—— "
             "因为镜像由 FRender 在 `OnAssetCreated` / `OnAssetImported` 里创建，"
             "参数必须随资源一起走到那里，而资产层不认识任何 RHI 类型。")
D.SetAccess("public")
D.Interface("explicit FTexture(std::string InPath)",
            "以路径构造空贴图（导入路径：codec 随后填字段）")
D.Interface("void Serialize(Archive::FArchive& Ar) override",
            "casset payload：容器头 + 像素快照（解码后的 CPU 数据）。字段布局只在这里声明一次，"
            "构造与持久化共用同一份布局")
D.Interface("[[nodiscard]] ETextureDimension GetDimension() const", "读维度")
D.Interface("[[nodiscard]] ETexturePixelFormat GetPixelFormat() const", "读像素格式")
D.Interface("[[nodiscard]] std::uint32_t GetWidth() const", "宽")
D.Interface("[[nodiscard]] std::uint32_t GetHeight() const", "高")
D.Interface("[[nodiscard]] std::uint32_t GetDepth() const", "3D 深度")
D.Interface("[[nodiscard]] std::uint32_t GetArrayLayers() const", "数组层数 / Cube 面数")
D.Interface("[[nodiscard]] std::uint32_t GetMipCount() const", "mip 级数")
D.Interface("[[nodiscard]] bool IsSRGB() const", "是否按 sRGB 解释")
D.Interface("[[nodiscard]] const std::vector<std::uint8_t>& GetPixels() const", "读像素（不可变）")
D.Interface("[[nodiscard]] std::vector<std::uint8_t>& GetPixelsMutable()", "写像素（导出前的 fill-back 路径用）")
D.Interface("[[nodiscard]] ETextureMirrorUsage GetMirrorUsage() const", "镜像用途（默认 Sampled）；"
                                                                        "由 `TResourceCreateDesc::Make` 设置，"
                                                                        "FRender 据此建 Persistent attachment")
D.Interface("void SetMirrorUsage(ETextureMirrorUsage Usage)", "设置镜像用途")
D.Interface("[[nodiscard]] ETextureSamplerMode GetFilterMode() const", "镜像 sampler 的滤波模式"
                                                                      "（由描述符设置，或保持导入默认）")
D.Interface("void SetFilterMode(ETextureSamplerMode Mode)", "设置滤波模式")
D.Interface("[[nodiscard]] ETextureAddressMode GetAddressU() const", "U 轴环绕")
D.Interface("void SetAddressU(ETextureAddressMode Mode)", "设置 U 轴环绕")
D.Interface("[[nodiscard]] ETextureAddressMode GetAddressV() const", "V 轴环绕")
D.Interface("void SetAddressV(ETextureAddressMode Mode)", "设置 V 轴环绕")
D.Interface("[[nodiscard]] ETextureAddressMode GetAddressW() const", "W 轴环绕")
D.Interface("void SetAddressW(ETextureAddressMode Mode)", "设置 W 轴环绕")
D.Interface("[[nodiscard]] float GetLodBias() const", "mip lod 偏移")
D.Interface("void SetLodBias(float Bias)", "设置 lod 偏移")
D.Interface("void ReleaseBulk() override",
            "渲染镜像取走像素后丢掉 CPU 载荷以回收内存（`clear` + `shrink_to_fit`，后者是必须的 —— "
            "只 clear 不会把大缓冲还给分配器）")
D.Interface("[[nodiscard]] bool HasBulk() const override", "CPU 载荷是否还在（导出时会据此触发 GPU fill-back）")
D.SetAccess("protected")
D.Interface("FTexture(std::string InPath, ETextureDimension InDimension, ETexturePixelFormat InFormat, "
            "std::uint32_t InWidth, std::uint32_t InHeight, std::uint32_t InDepth, "
            "std::uint32_t InArrayLayers, std::uint32_t InMipCount, bool bInSRGB, "
            "std::vector<std::uint8_t> InPixels)",
            "像素 payload 构造：各维度子类以自身参数转发到它，字段布局仍然只有一处")
D.Field("ETextureDimension Dimension = ETextureDimension::Tex2D", "维度")
D.Field("ETexturePixelFormat PixelFormat = ETexturePixelFormat::Unknown", "像素格式")
D.Field("std::uint32_t Width = 0", "宽")
D.Field("std::uint32_t Height = 0", "高")
D.Field("std::uint32_t Depth = 1", "深度")
D.Field("std::uint32_t ArrayLayers = 1", "数组层数")
D.Field("std::uint32_t MipCount = 1", "mip 级数")
D.Field("bool bSRGB = true", "是否 sRGB")
D.Field("std::vector<std::uint8_t> Pixels", "像素字节（镜像取走后被 `ReleaseBulk` 清空）")
D.Field("ETextureMirrorUsage MirrorUsage = ETextureMirrorUsage::Sampled", "镜像用途")
D.Field("ETextureSamplerMode FilterMode = ETextureSamplerMode::Linear", "滤波模式")
D.Field("ETextureAddressMode AddressU = ETextureAddressMode::Repeat", "U 环绕")
D.Field("ETextureAddressMode AddressV = ETextureAddressMode::Repeat", "V 环绕")
D.Field("ETextureAddressMode AddressW = ETextureAddressMode::Repeat", "W 环绕")
D.Field("float LodBias = 0.0f", "lod 偏移")

D.Class("FTexture1D", Base="FTexture",
        Desc="一维贴图的具体类型。存在理由不是「多态」（载荷全在基类）而是**类型**：`Import<FTexture1D>` / "
             "`CreateResource<FTexture1D>` 需要一个确定类型来选特化，维度差异则收敛在那个维度专用的 payload 构造函数里。")
D.SetAccess("public")
D.Interface("explicit FTexture1D(std::string InPath)", "以路径构造（导入路径）")
D.Interface("FTexture1D(std::string InPath, ETexturePixelFormat InFormat, std::uint32_t InWidth, "
            "std::uint32_t InArrayLayers, std::uint32_t InMipCount, bool bInSRGB, "
            "std::vector<std::uint8_t> InPixels)",
            "维度专用 payload 构造：高 / 深固定为 1，调用方无法给出矛盾的维度组合")

D.Class("FTexture2D", Base="FTexture",
        Desc="二维贴图的类型（最常用的一类，也是唯一有 `TResourceCreateDesc` 特化的 —— "
             "运行期程序化建贴图目前只走 2D，其余维度按需再加描述符特化）。")
D.SetAccess("public")
D.Interface("explicit FTexture2D(std::string InPath)", "以路径构造（导入路径）")
D.Interface("FTexture2D(std::string InPath, ETexturePixelFormat InFormat, std::uint32_t InWidth, "
            "std::uint32_t InHeight, std::uint32_t InMipCount, bool bInSRGB, "
            "std::vector<std::uint8_t> InPixels)",
            "维度专用 payload 构造：深度 / 层数固定为 1")

D.Class("FTexture3D", Base="FTexture", Desc="三维贴图类型（体积数据）。")
D.SetAccess("public")
D.Interface("explicit FTexture3D(std::string InPath)", "以路径构造（导入路径）")
D.Interface("FTexture3D(std::string InPath, ETexturePixelFormat InFormat, std::uint32_t InWidth, "
            "std::uint32_t InHeight, std::uint32_t InDepth, std::uint32_t InMipCount, bool bInSRGB, "
            "std::vector<std::uint8_t> InPixels)",
            "维度专用 payload 构造：这里才需要深度")

D.Class("FTextureCube", Base="FTexture",
        Desc="立方体贴图类型。**面数 6 由构造函数写死**（`ArrayLayers = 6`）—— "
             "调用方给不出「立方体却有 5 层」这种矛盾状态。")
D.SetAccess("public")
D.Interface("explicit FTextureCube(std::string InPath)", "以路径构造（导入路径）")
D.Interface("FTextureCube(std::string InPath, ETexturePixelFormat InFormat, std::uint32_t InSize, "
            "std::uint32_t InMipCount, bool bInSRGB, std::vector<std::uint8_t> InPixels)",
            "维度专用 payload 构造：只收边长，宽 = 高 = Size，层数恒为 6")

D.Class("FTexture2DArray", Base="FTexture", Desc="二维数组贴图类型（同尺寸的多层 2D，层数可变）。")
D.SetAccess("public")
D.Interface("explicit FTexture2DArray(std::string InPath)", "以路径构造（导入路径）")
D.Interface("FTexture2DArray(std::string InPath, ETexturePixelFormat InFormat, std::uint32_t InWidth, "
            "std::uint32_t InHeight, std::uint32_t InArrayLayers, std::uint32_t InMipCount, bool bInSRGB, "
            "std::vector<std::uint8_t> InPixels)",
            "维度专用 payload 构造：深度固定为 1，层数由调用方给出")

D.Class("FTextureCubeArray", Base="FTexture", Desc="立方体数组贴图类型（6 x N 面，常用于环境探针集合）。")
D.SetAccess("public")
D.Interface("explicit FTextureCubeArray(std::string InPath)", "以路径构造（导入路径）")
D.Interface("FTextureCubeArray(std::string InPath, ETexturePixelFormat InFormat, std::uint32_t InSize, "
            "std::uint32_t InArrayLayers, std::uint32_t InMipCount, bool bInSRGB, "
            "std::vector<std::uint8_t> InPixels)",
            "维度专用 payload 构造：边长 + 层数")

D.Struct("TResourceCreateDesc<FTexture2D>", Base="模板特化（描述符 → 已填好的资源）",
         Desc="`FResourceSystem::CreateResource<FTexture2D>` 的描述符：携带建成一张 GPU 贴图所需的全部参数，"
              "由 `Make` 填进资源实例。`Pixels` 为空表示「只建镜像、不上传内容」。"
              "描述符是值语义的小记录，`Make` 负责把里面的参数分发到资源的 setter 上"
              "（含镜像 usage / 采样参数），这样镜像的创建参数与资源实例不会各说各话。")
D.SetAccess("public")
D.Interface("[[nodiscard]] static FTexture2D Make(std::string_view Path, const FConfig& Config)",
            "按描述符造出资源：先构造 `FTexture2D`（维度 / 格式 / 尺寸 / mip / sRGB / 像素），"
            "再把镜像 usage、滤波、三轴环绕、lod bias 逐个写进资源，供 FRender 的 "
            "`OnAssetMirrorCreated` 建 Persistent 镜像时读取")
D.Nested("FConfig", Kind="struct", Desc="创建参数集，默认值即「一张默认的 2D 采样贴图」")
D.Field("ETexturePixelFormat Format = ETexturePixelFormat::RGBA8", "像素格式")
D.Field("std::uint32_t Width = 0 / Height = 0", "尺寸（须由调用方给出）")
D.Field("std::uint32_t ArrayLayers = 1", "层数")
D.Field("std::uint32_t MipCount = 1", "mip 级数")
D.Field("bool bSRGB = true", "是否 sRGB")
D.Field("ETextureMirrorUsage Usage = ETextureMirrorUsage::Sampled", "镜像用途")
D.Field("ETextureSamplerMode FilterMode = ETextureSamplerMode::Linear", "镜像 sampler 滤波")
D.Field("ETextureAddressMode AddressU / AddressV / AddressW = ETextureAddressMode::Repeat", "镜像 sampler 三轴环绕")
D.Field("float LodBias = 0.0f", "镜像 sampler lod 偏移")
D.Field("std::vector<std::uint8_t> Pixels", "像素内容；空表示只建镜像、不上传")

D.Class("FStaticMesh", Base="FAssetsResource",
        Desc="静态网格资产：顶点位置 / 法线 / UV + 索引 + 材质引用。"
             "几何数据以扁平的 `std::vector<float>` 存放（而不是顶点结构体数组）—— "
             "这样磁盘布局与内存布局都是「同一个 float 序列」，codec 不需要处理结构体 padding。")
D.SetAccess("public")
D.Interface("explicit FStaticMesh(std::string InPath)", "以路径构造（类型标记为 StaticMesh）")
D.Interface("void Serialize(Archive::FArchive& Ar) override", "casset payload：容器头 + 几何数据")
D.Interface("[[nodiscard]] const std::string& GetMaterial() const", "读材质资产路径")
D.Interface("[[nodiscard]] const std::vector<float>& GetPositions() const", "读顶点位置（每顶点 3 float）")
D.Interface("[[nodiscard]] const std::vector<float>& GetNormals() const", "读法线（每顶点 3 float）")
D.Interface("[[nodiscard]] const std::vector<float>& GetUVs() const", "读 UV（每顶点 2 float）")
D.Interface("[[nodiscard]] const std::vector<std::uint32_t>& GetIndices() const", "读索引")
D.SetAccess("protected")
D.Interface("FStaticMesh(std::string InPath, std::string InMaterial, std::vector<float> InPositions, "
            "std::vector<float> InNormals, std::vector<float> InUVs, std::vector<std::uint32_t> InIndices)",
            "几何 payload 构造：字段布局只在 `Serialize` 声明一次（构造与持久化共用）")
D.Field("std::string MaterialPath", "材质资产路径（软引用：按路径在目录里解析，不做指针绑定）")
D.Field("std::vector<float> Positions", "顶点位置")
D.Field("std::vector<float> Normals", "顶点法线")
D.Field("std::vector<float> UVs", "顶点 UV")
D.Field("std::vector<std::uint32_t> Indices", "三角形索引")

D.Class("FSkeleton", Base="FAssetsResource", Desc="骨骼资产：一列骨骼记录（名字 + 父索引 + 绑定矩阵）。")
D.SetAccess("public")
D.Interface("explicit FSkeleton(std::string InPath)", "以路径构造（类型标记为 Skeleton）")
D.Interface("void Serialize(Archive::FArchive& Ar) override", "casset payload：容器头 + 骨骼表")
D.Interface("[[nodiscard]] const std::vector<FSkeletonBone>& GetBones() const", "读骨骼表")
D.Interface("void SetBones(std::vector<FSkeletonBone> InBones)", "整体替换骨骼表（解码后一次写入）")
D.SetAccess("protected")
D.Field("std::vector<FSkeletonBone> Bones", "骨骼表（按父先子后的顺序存放，读回即可直接建层级）")

D.Class("FAnimation", Base="FAssetsResource",
        Desc="动画资产：指向骨骼资产的软引用 + 时长 + 轨道表。"
             "骨骼用**路径**引用而不是指针 —— 资产之间靠路径解析，加载顺序因此不受约束。")
D.SetAccess("public")
D.Interface("explicit FAnimation(std::string InPath)", "以路径构造（类型标记为 Animation）")
D.Interface("void Serialize(Archive::FArchive& Ar) override", "casset payload：容器头 + 骨骼引用 + 时长 + 轨道")
D.Interface("[[nodiscard]] const std::string& GetSkeleton() const", "读骨骼资产路径")
D.Interface("void SetSkeleton(std::string Path)", "设置骨骼资产路径")
D.Interface("[[nodiscard]] float GetDurationSeconds() const", "动画时长（秒）")
D.Interface("void SetDurationSeconds(float Seconds)", "设置时长")
D.Interface("[[nodiscard]] const std::vector<FAnimationTrack>& GetTracks() const", "读轨道表")
D.Interface("void SetTracks(std::vector<FAnimationTrack> InTracks)", "整体替换轨道表")
D.SetAccess("protected")
D.Field("std::string SkeletonPath", "骨骼资产路径（软引用）")
D.Field("float DurationSeconds = 0.f", "时长（秒）")
D.Field("std::vector<FAnimationTrack> Tracks", "轨道表")

D.Class("FMaterial", Base="FAssetsResource",
        Desc="材质资产：五张贴图引用 + 颜色 / 金属度 / 粗糙度 / 自发光因子。"
             "**注意它的因子字段是 public、路径字段是 protected** —— 因子是渲染与编辑面板都要频繁读写的标量，"
             "路径则只应通过 getter/setter 成对访问（保证引用表的维护口径一致）。"
             "它的 `Serialize` 直接内联在头里，因为字段全是 POD，无需隐藏实现。")
D.SetAccess("public")
D.Interface("explicit FMaterial(std::string InPath)", "以路径构造（类型标记为 Material）")
D.Interface("[[nodiscard]] const std::string& GetBaseColorTexture() const", "读基础色贴图路径")
D.Interface("void SetBaseColorTexture(std::string Path)", "设置基础色贴图路径")
D.Interface("[[nodiscard]] const std::string& GetNormalTexture() const", "读法线贴图路径")
D.Interface("void SetNormalTexture(std::string Path)", "设置法线贴图路径")
D.Interface("[[nodiscard]] const std::string& GetMetallicRoughnessTexture() const", "读金属度-粗糙度贴图路径")
D.Interface("void SetMetallicRoughnessTexture(std::string Path)", "设置金属度-粗糙度贴图路径")
D.Interface("[[nodiscard]] const std::string& GetOcclusionTexture() const", "读遮蔽贴图路径")
D.Interface("void SetOcclusionTexture(std::string Path)", "设置遮蔽贴图路径")
D.Interface("[[nodiscard]] const std::string& GetEmissiveTexture() const", "读自发光贴图路径")
D.Interface("void SetEmissiveTexture(std::string Path)", "设置自发光贴图路径")
D.Interface("void Serialize(Archive::FArchive& Ar) override",
            "casset payload：**先** `FAssetsResource::Serialize` 写容器头，**再**追加材质业务字段"
            "（五条路径 + 四个基础色分量 + 金属度/粗糙度 + 三个自发光分量）。顺序与读回时一致")
D.Field("float BaseColorFactor[4] = {1, 1, 1, 1}", "基础色因子（RGBA）")
D.Field("float MetallicFactor = 0.f", "金属度")
D.Field("float RoughnessFactor = 1.f", "粗糙度")
D.Field("float EmissiveFactor[3] = {0, 0, 0}", "自发光颜色")
D.SetAccess("protected")
D.Field("std::string BaseColorPath", "基础色贴图路径（软引用）")
D.Field("std::string NormalPath", "法线贴图路径")
D.Field("std::string MetallicRoughnessPath", "金属度-粗糙度贴图路径")
D.Field("std::string OcclusionPath", "遮蔽贴图路径")
D.Field("std::string EmissivePath", "自发光贴图路径")

D.Class("FAnimationGraph", Base="FAssetsResource",
        Desc="动画图的**文档型**持有者：内容是一段 JSON，引擎 core 不解释它。"
             "引擎只负责把文档存下来、按路径取用 —— 图的结构归上层（编辑器 / 运行时那侧）定义，"
             "所以升级图格式不需要动引擎核心的 codec。")
D.SetAccess("public")
D.Interface("explicit FAnimationGraph(std::string InPath)", "以路径构造（类型标记为 AnimationGraph）")
D.Interface("[[nodiscard]] const std::string& GetDocumentJson() const", "读文档 JSON")
D.Interface("void SetDocumentJson(std::string Json)", "整体写入文档 JSON")
D.Interface("void Serialize(Archive::FArchive& Ar) override",
            "casset payload：先写容器头，再追加文档字段（一个字符串，故格式演化只影响文档自身）")
D.SetAccess("protected")
D.Field("std::string DocumentJson", "文档内容（引擎不解析）")

D.Class("FPrefab", Base="FAssetsResource",
        Desc="预制体：同样是文档型持有者（JSON 正文），与 `FAnimationGraph` 结构一致但资产类型不同 —— "
             "类型不同意味着 importer / exporter / creator 各走自己的特化，"
             "在目录里也不会互相覆盖。")
D.SetAccess("public")
D.Interface("explicit FPrefab(std::string InPath)", "以路径构造（类型标记为 Prefab）")
D.Interface("[[nodiscard]] const std::string& GetDocumentJson() const", "读文档 JSON")
D.Interface("void SetDocumentJson(std::string Json)", "整体写入文档 JSON")
D.Interface("void Serialize(Archive::FArchive& Ar) override", "casset payload：先写容器头，再追加文档字段")
D.SetAccess("protected")
D.Field("std::string DocumentJson", "文档内容（引擎不解析）")
