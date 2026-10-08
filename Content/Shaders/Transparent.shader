// Transparent —— 与 UnlitColor 唯一的区别就是 render state：Blend SrcAlpha OneMinusSrcAlpha +
// ZWrite Off。它存在的理由是"状态键真的进了 pipeline"这件事有一个能被眼睛看到的证据：挡住了
// 后面的东西才算生效。

Shader "Transparent"
{
	Properties
	{
		_BaseColor("Base Color", Color) = (0.20, 0.65, 0.95, 0.35)
	}

	SubShader
	{
		Pass
		{
			Name "Transparent"
			Cull Back
			ZWrite Off
			ZTest LEqual
			Blend SrcAlpha OneMinusSrcAlpha

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
