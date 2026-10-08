// DefaultLit —— 本项目的默认材质：Lambert 直接光 + 一张 base map。
//
// 这份文本就是"简易 Unity 语法"的全部：Properties 决定 set2 binding0 的常量块与采样器，
// Pass 里的四个状态键进 pipeline，两个 struct 决定顶点输入与阶段间的 layout(location)。
// 引擎把它重写成 GLSL 并注入引擎块（见 ShaderLibrary 的 design D3/D4），文件里不写一行
// Vulkan 细节 —— 那正是这一层的存在理由。

Shader "DefaultLit"
{
	Properties
	{
		_BaseColor("Base Color", Color) = (0.90, 0.10, 0.90, 1.0)
		_Metallic("Metallic", Range(0.0, 1.0)) = 0.05
		_BaseMap("Base Map", 2D) = "white" {}
	}

	SubShader
	{
		Pass
		{
			Name "Forward"
			Cull Back
			ZWrite On
			ZTest LEqual
			Blend Off

			#pragma vertex VSMain
			#pragma fragment PSMain

			// a2v：顶点流。类型/成员名随便写，语义必须是语义表里的那六个之一
			// (POSITION=0 / NORMAL=1 / TEXCOORD0=2 / TEXCOORD1=3 / TANGENT=4 / COLOR0=5)。
			struct a2v
			{
				float3 position : POSITION;
				float3 normal   : NORMAL;
				float2 uv       : TEXCOORD0;
			};

			// v2f：阶段间。SV_POSITION 对应 gl_Position，SV_Target 是片元的颜色输出，
			// 其余成员按语义表拿 location。
			struct v2f
			{
				float4 position : SV_POSITION;
				float3 normal   : TEXCOORD0;
				float2 uv       : TEXCOORD1;
				float4 color    : SV_Target;
			};

			// 参数与返回值都会被"溶解"成全局量（v.position -> m_v_position 这样），
			// 函数体里按 实例名.成员名 访问就行，其余照 GLSL 写 —— 包括类型名：
			// struct 成员写 float3/float4（那边有语义表要重写），函数体里写 vec3/vec4。
			void VSMain(a2v v, inout v2f i)
			{
				vec4 World = _Push.ObjectMatrix * vec4(v.position, 1.0);
				i.position = _Frame.ViewProj * World;
				i.normal = mat3(_Push.ObjectMatrix) * v.normal;
				i.uv = v.uv;
			}

			void PSMain(v2f i)
			{
				vec3 N = normalize(i.normal);
				vec3 L = normalize(vec3(0.35, 0.80, -0.45));
				float Diffuse = max(dot(N, L), 0.0);
				vec3 Albedo = _Material._BaseColor.rgb * texture(_BaseMap, i.uv).rgb;
				i.color = vec4(Albedo * (0.25 + 0.75 * Diffuse), _Material._BaseColor.a);
			}
		}
	}
}
