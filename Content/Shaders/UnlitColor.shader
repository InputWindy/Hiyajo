// UnlitColor —— 一个纯色 unlit：只有 _BaseColor，不采样任何纹理。
// 存在理由是"共享材质 + 不同材质"的对照：两个 actor 用同一份 shader 的不同参数，或者一个用
// 这个、另一个用 DefaultLit。

Shader "UnlitColor"
{
	Properties
	{
		_BaseColor("Base Color", Color) = (0.85, 0.20, 0.85, 1.0)
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

			struct a2v
			{
				float3 position : POSITION;
			};

			struct v2f
			{
				float4 position : SV_POSITION;
				float4 color    : SV_Target;
			};

			void VSMain(a2v v, inout v2f i)
			{
				i.position = _Frame.ViewProj * (_Push.ObjectMatrix * vec4(v.position, 1.0));
			}

			void PSMain(v2f i)
			{
				i.color = _Material._BaseColor;
			}
		}
	}
}
