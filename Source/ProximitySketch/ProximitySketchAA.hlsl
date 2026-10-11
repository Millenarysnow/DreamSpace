// 游戏用平滑的距离限制：只平滑新增墨线，玩家内圈直接通过原始 TSR 结果。
// 用与主材质相同的去抖深度 / 逆投影重建球形遮罩，不能复用默认取整的
// WorldPosition；两阶段的边界必须一致，防止平滑影响正常渲染的区域。
float2 viewportUV = GetViewportUV(Parameters);
float4 size = GetSceneTextureViewSize(PPI_SceneDepth);
float2 jitteredUV = viewportUV + ResolvedView.TemporalAAParams.zw * size.zw;
float2 continuousPixel = jitteredUV * size.xy - 0.5;
float2 basePixel = floor(continuousPixel);
float2 blend = frac(continuousPixel);
float inverseDepth = 0.0;
float surfaceMask = 0.0;
[unroll]
for (int tap = 0; tap < 4; ++tap)
{
    float2 corner = float2(tap & 1, tap >> 1);
    float2 tapViewportUV = (basePixel + corner + 0.5) * size.zw;
    float2 depthUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(tapViewportUV, PPI_SceneDepth), PPI_SceneDepth);
    float depth = max(SceneTextureLookup(depthUV, PPI_SceneDepth, false).r, 1.0);
    // 与主通道一致：241 标记的手办可见像素使用前景透明面的实际深度。
    float customDepth = SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(tapViewportUV,
        PPI_CustomDepth), PPI_CustomDepth), PPI_CustomDepth, false).r;
    float stencil = SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(tapViewportUV,
        PPI_CustomStencil), PPI_CustomStencil), PPI_CustomStencil, false).r;
    if (abs(stencil - 241.0) < 0.5 && customDepth < depth)
        depth = max(customDepth, 1.0);
    float weight = (corner.x > 0.5 ? blend.x : 1.0 - blend.x) * (corner.y > 0.5 ? blend.y : 1.0 - blend.y);
    inverseDepth += weight / depth;
    surfaceMask += weight * (1.0 - step(MaxSubjectDepth, depth));
}
float2 bufferPixel = ViewportUVToSceneTextureUV(jitteredUV, PPI_SceneDepth)
    * GetSceneTextureBufferSize(PPI_SceneDepth).xy;
float3 surfacePosition = DFHackToFloat(SvPositionToWorld(float4(bufferPixel,
    ConvertToDeviceZ(rcp(max(inverseDepth, 0.00000001))), 1)));
float distanceToPlayer = length((surfacePosition - PlayerPosition.rgb) * surfaceMask);
float sketchWeight = lerp(1.0, smoothstep(max(NormalRadius, 0.0),
    max(NormalRadius, 0.0) + max(TransitionWidth, 0.01), distanceToPlayer), surfaceMask) * saturate(EffectStrength);
// 调试视图要保留精确的遮罩 / 原始颜色，内圈也不执行额外颜色滤波。
if (SketchDebugView > 0.5 || sketchWeight <= 0.0)
    return SceneColor.rgb;

// 生成器在此嵌入已验收的 WhiteOutlineAA.hlsl，将最后的返回按距离权重混合。
// 整段代码存入材质 Custom 节点，打包运行无需依赖外部文件。
// PROXIMITY_AA_IMPLEMENTATION
