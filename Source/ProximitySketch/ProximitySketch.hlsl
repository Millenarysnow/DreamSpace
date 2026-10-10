// 游戏用的距离线稿后处理，运行于 After Tonemapping。
// 先让原始场景完成曝光、TAA / TSR 和调色，再合成白纸，防止大片白纸参与
// 自动曝光计量、把本应正常渲染的内圈压暗。内圈直接返回最终场景颜色。
// 所有空间扰动都固定在世界坐标上，没有 Time 输入，也不移动建筑的轮廓。

struct ProximityPencil
{
    float Hash(float2 p)
    {
        return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
    }

    float Noise(float2 p)
    {
        float2 cell = floor(p);
        float2 blend = frac(p);
        blend = blend * blend * (3.0 - 2.0 * blend);
        return lerp(lerp(Hash(cell), Hash(cell + float2(1, 0)), blend.x),
                    lerp(Hash(cell + float2(0, 1)), Hash(cell + float2(1, 1)), blend.x), blend.y);
    }

    // 沿用已验收预览的落笔、笔压、错开接笔及远距离导数过滤。
    // activation 控制已有笔划的长度和浓度，而不是每帧重新随机生成线条。
    float Stroke(float2 coordinate, float activation, float width, float irregularity, float strokeLength, float seed)
    {
        float row = floor(coordinate.y + 0.5);
        float rowOffset = (Hash(float2(row, seed + 8.0)) * 2.0 - 1.0) * irregularity;
        float bend = rowOffset + (Noise(float2(coordinate.x * 0.32, row * 1.71 + seed)) * 2.0 - 1.0)
            * irregularity + sin(coordinate.x * 1.37 + row * 2.11 + seed) * irregularity * 0.25;
        float distanceToLine = abs(frac(coordinate.y + bend + 0.5) - 0.5);
        float footprint = max(length(float2(ddx(coordinate.y), ddy(coordinate.y))), 0.0001);
        float pressure = lerp(0.78, 1.16, Noise(float2(coordinate.x * 0.6, row + seed)));
        float halfWidth = min(width * pressure * footprint * 0.5, 0.22);
        float coverage = saturate((halfWidth - distanceToLine) / footprint + 0.5);
        float along = coordinate.x / max(strokeLength, 1.0) + Hash(float2(row, seed)) * 0.8;
        float segmentLength = lerp(0.42, 0.95, activation)
            * lerp(0.86, 1.0, Hash(float2(floor(along) + seed, row)));
        float segmentAA = max(fwidth(along), 0.002);
        float ends = smoothstep(0.0, segmentAA, frac(along))
            * (1.0 - smoothstep(segmentLength - segmentAA, segmentLength + segmentAA, frac(along)));
        float graphite = lerp(0.78, 1.0, Noise(coordinate * float2(3.2, 7.0) + seed));
        float resolved = coverage * ends * graphite;
        float average = min(width * footprint, 0.44) * segmentLength * 0.88;
        return lerp(resolved, average, smoothstep(0.3, 0.65, footprint)) * activation;
    }

    // GBuffer 不能直接硬件双线性采样。显式重建四个深度 / 法线 texel，
    // 先加上当前帧的投影抖动，得到与已去抖的最终画面对齐的场景缓冲坐标。
    // 深度插值使用倒数深度：同一透视平面沿屏幕近似线性，斜墙不会变成假边缘。
    float4 Attributes(float2 viewportUV, float maximumDepth, out float surfaceCoverage)
    {
        float4 size = GetSceneTextureViewSize(PPI_SceneDepth);
        float2 jitteredUV = viewportUV + ResolvedView.TemporalAAParams.zw * size.zw;
        float2 continuousPixel = jitteredUV * size.xy - 0.5;
        float2 basePixel = floor(continuousPixel);
        float2 blend = frac(continuousPixel);
        float inverseDepth = 0.0;
        float3 normal = float3(0, 0, 0);
        surfaceCoverage = 0.0;
        [unroll]
        for (int tap = 0; tap < 4; ++tap)
        {
            float2 corner = float2(tap & 1, tap >> 1);
            float2 tapViewportUV = (basePixel + corner + 0.5) * size.zw;
            float2 depthUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(tapViewportUV, PPI_SceneDepth), PPI_SceneDepth);
            float2 normalUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(tapViewportUV, PPI_WorldNormal), PPI_WorldNormal);
            float depth = max(SceneTextureLookup(depthUV, PPI_SceneDepth, false).r, 1.0);
            float mask = 1.0 - step(maximumDepth, depth);
            float weight = (corner.x > 0.5 ? blend.x : 1.0 - blend.x)
                * (corner.y > 0.5 ? blend.y : 1.0 - blend.y);
            inverseDepth += weight / depth;
            normal += SceneTextureLookup(normalUV, PPI_WorldNormal, false).rgb * weight * mask;
            surfaceCoverage += weight * mask;
        }
        return float4(normalize(normal + float3(0.00001, 0, 0)), inverseDepth);
    }
};
ProximityPencil pencil;

float2 viewportUV = GetViewportUV(Parameters);
float surfaceMask;
float4 centerAttributes = pencil.Attributes(viewportUV, MaxSubjectDepth, surfaceMask);
float centerDepth = rcp(max(centerAttributes.w, 0.00000001));
float3 centerNormal = centerAttributes.xyz;
// 世界位置也必须使用同一份去抖的深度，不能再使用默认 WorldPosition 节点：
// 默认节点取整采样会在轮廓旁切换到另一个表面，令距离边界和面内笔划滑动。
float4 depthSize = GetSceneTextureViewSize(PPI_SceneDepth);
float2 jitteredViewportUV = viewportUV + ResolvedView.TemporalAAParams.zw * depthSize.zw;
float2 sceneUV = ViewportUVToSceneTextureUV(jitteredViewportUV, PPI_SceneDepth);
float2 bufferPixel = sceneUV * GetSceneTextureBufferSize(PPI_SceneDepth).xy;
float deviceDepth = ConvertToDeviceZ(centerDepth);
float3 surfacePosition = DFHackToFloat(SvPositionToWorld(float4(bufferPixel, deviceDepth, 1)));

// 手办透明显示不在普通 GBuffer 中；它的专用材质按可见透明度写 241 Stencil。
// 只为“距离判定”选择它的前景深度，轮廓 / 法线仍来自不透明建筑。
// 同时检查比不透明表面更近，避免被墙遮住的 CustomDepth 反过来污染遮罩。
float revealInverseDepth = 0.0;
float revealCoverage = 0.0;
float2 revealPixel = jitteredViewportUV * depthSize.xy - 0.5;
float2 revealBase = floor(revealPixel);
float2 revealBlend = frac(revealPixel);
[unroll]
for (int tap = 0; tap < 4; ++tap)
{
    float2 corner = float2(tap & 1, tap >> 1);
    float2 tapUV = (revealBase + corner + 0.5) * depthSize.zw;
    float opaqueDepth = max(SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(tapUV,
        PPI_SceneDepth), PPI_SceneDepth), PPI_SceneDepth, false).r, 1.0);
    float customDepth = SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(tapUV,
        PPI_CustomDepth), PPI_CustomDepth), PPI_CustomDepth, false).r;
    float stencil = SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(tapUV,
        PPI_CustomStencil), PPI_CustomStencil), PPI_CustomStencil, false).r;
    float distanceDepth = abs(stencil - 241.0) < 0.5 && customDepth < opaqueDepth ? max(customDepth, 1.0) : opaqueDepth;
    float weight = (corner.x > 0.5 ? revealBlend.x : 1.0 - revealBlend.x)
        * (corner.y > 0.5 ? revealBlend.y : 1.0 - revealBlend.y);
    revealInverseDepth += weight / distanceDepth;
    revealCoverage += weight * (1.0 - step(MaxSubjectDepth, distanceDepth));
}
float3 revealPosition = DFHackToFloat(SvPositionToWorld(float4(bufferPixel,
    ConvertToDeviceZ(rcp(max(revealInverseDepth, 0.00000001))), 1)));

// 遮罩使用完整三维世界距离，中心由当前本地控制器的 Pawn 提供。
// 同一面墙可跨越边界，镜头旋转、拉远或改变重力方向不会移动这个球形范围。
// 没有有限深度的天空视作范围外的白纸；不对它的无穷大世界位置计算距离。
float distanceToPlayer = length((revealPosition - PlayerPosition.rgb) * revealCoverage);
float radius = max(NormalRadius, 0.0);
float transition = max(TransitionWidth, 0.01);
float sketchWeight = lerp(1.0, smoothstep(radius, radius + transition, distanceToPlayer), revealCoverage)
    * saturate(EffectStrength);

// 最终颜色已经是输出分辨率；场景缓冲仍可能低于该分辨率，Attributes 中
// 单独转换 GBuffer UV，并对其连续采样，避免窗口缩放和 TSR 降采样后错位。
float2 inputPixelUV = GetSceneTextureViewSize(PPI_PostProcessInput0).zw;
float2 finalPixelUV = inputPixelUV;
float2 pixelPosition = viewportUV / max(finalPixelUV, float2(0.000001, 0.000001));
float pressure = sin(dot(pixelPosition, float2(0.093, 0.048))) * 0.6
    + sin(dot(pixelPosition, float2(0.019, -0.077)) + 1.7) * 0.4;
float widthScale = 1.0 + clamp(StrokeVariation, 0.0, 0.45) * pressure;
// 轮廓与笔划以输出像素计宽，过细的线保留半像素覆盖率。
float2 outerStep = max(finalPixelUV * SilhouetteWidth * widthScale, inputPixelUV * 0.5);
float2 innerStep = max(finalPixelUV * StructureWidth * widthScale, inputPixelUV * 0.5);
float silhouetteEdge = 0.0;
float normalEdge = 0.0;
float depthEdge = 0.0;
const float2 directions[4] = {
    float2(1, 0), float2(0, 1), float2(0.70710678, 0.70710678), float2(0.70710678, -0.70710678)
};
[unroll]
for (int axis = 0; axis < 4; ++axis)
{
    float pairDepth[2];
    float pairMask[2];
    [unroll]
    for (int side = 0; side < 2; ++side)
    {
        float2 direction = directions[axis] * (side == 0 ? -1.0 : 1.0);
        float outerMask;
        pencil.Attributes(viewportUV + direction * outerStep, MaxSubjectDepth, outerMask);
        silhouetteEdge = max(silhouetteEdge, abs(surfaceMask - outerMask));
        float2 innerUV = viewportUV + direction * innerStep;
        float sampleMask;
        float4 attributes = pencil.Attributes(innerUV, MaxSubjectDepth, sampleMask);
        float sampleDepth = rcp(max(attributes.w, 0.00000001));
        float3 sampleNormal = attributes.xyz;
        pairDepth[side] = sampleDepth;
        pairMask[side] = sampleMask;
        float crease = smoothstep(NormalThreshold, NormalThreshold + 0.16,
            1.0 - saturate(dot(centerNormal, sampleNormal)));
        // 天空边缘处插值的法线 / 深度并不代表真实表面。该位置只使用
        // 连续覆盖率的 silhouetteEdge，避免天空参与结构差分产生跳动的粗黑阶梯。
        float validSurfaces = smoothstep(0.97, 1.0, min(surfaceMask, sampleMask));
        normalEdge = max(normalEdge, crease * validSurfaces);
    }
    // 倒数深度二阶差分能过滤斜墙本身的透视梯度，只标记遮挡 / 台阶。
    float curvature = abs(centerDepth / pairDepth[0] + centerDepth / pairDepth[1] - 2.0);
    float discontinuity = smoothstep(DepthThreshold, DepthThreshold * 2.0, curvature);
    float validDepthPair = smoothstep(0.97, 1.0, min(surfaceMask, min(pairMask[0], pairMask[1])));
    depthEdge = max(depthEdge, discontinuity * validDepthPair
        * step(centerDepth, max(pairDepth[0], pairDepth[1])));
}

// 原场景不是白模。从色调映射前的 HDR 输入读取照明，再用基础色亮度
// 抵消材质颜色，避免对已经压缩的显示颜色直接做除法。
// 避免深色墙纸直接被当成阴影。极黑和金属表面无法可靠分离光照与反射，
// 使用连续的可靠度权重退回法线受光；该估计不是直接读取阴影缓冲。
float3 luminanceWeights = float3(0.2126, 0.7152, 0.0722);
float2 alignedBaseUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(jitteredViewportUV, PPI_BaseColor), PPI_BaseColor);
float2 alignedMetalUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(jitteredViewportUV, PPI_Metallic), PPI_Metallic);
float baseLuminance = dot(SceneTextureLookup(alignedBaseUV, PPI_BaseColor, false).rgb, luminanceWeights);
float exposure = max(Exposure, 0.0001);
float illumination = dot(max(LightingColor.rgb, 0.0), luminanceWeights) * ResolvedView.OneOverPreExposure
    * exposure / max(baseLuminance, 0.04);
float sceneShade = 1.0 - smoothstep(0.04, max(ShadowReference, 0.05), illumination);
float geometryShade = 1.0 - saturate(dot(centerNormal, normalize(LightDirection.rgb)));
float lightReliability = smoothstep(0.025, 0.12, baseLuminance)
    * (1.0 - saturate(SceneTextureLookup(alignedMetalUV, PPI_Metallic, false).r));
float tone = saturate((lerp(geometryShade, sceneShade, saturate(ShadowInfluence) * lightReliability)
    - HatchToneBias) * max(HatchToneContrast, 0.01));

float primaryActivation = smoothstep(0.08, 0.32, tone);
float crossActivation = smoothstep(0.43, 0.85, tone);
float denseActivation = smoothstep(0.87, 1.0, tone);
float3 projectionWeights = pow(abs(centerNormal), 8.0);
projectionWeights /= max(dot(projectionWeights, float3(1, 1, 1)), 0.0001);
float3 surface = surfacePosition * surfaceMask / max(HatchSpacing, 1.0);
float angle = HatchAngle * 0.017453293;
float2 primaryAlong = float2(cos(angle), sin(angle));
float2 primaryAcross = float2(-sin(angle), cos(angle));
float2 crossAlong = float2(cos(angle + 1.04719755), sin(angle + 1.04719755));
float2 crossAcross = float2(-sin(angle + 1.04719755), cos(angle + 1.04719755));
float hatch = 0.0;
[unroll]
for (int projection = 0; projection < 3; ++projection)
{
    float2 projected = projection == 0 ? surface.yz : (projection == 1 ? surface.xz : surface.xy);
    float2 primary = float2(dot(projected, primaryAlong), dot(projected, primaryAcross));
    float2 cross = float2(dot(projected, crossAlong), dot(projected, crossAcross)) / 1.18;
    float seed = 5.37 + projection * 13.1;
    float first = pencil.Stroke(primary, primaryActivation, HatchWidth,
        HatchIrregularity, HatchStrokeLength, seed);
    float second = pencil.Stroke(cross, crossActivation, HatchWidth,
        HatchIrregularity, HatchStrokeLength * 0.83, seed + 2.1);
    float third = pencil.Stroke(primary + float2(0.37, 0.5), denseActivation, HatchWidth * 0.9,
        HatchIrregularity, HatchStrokeLength, seed + 4.3);
    hatch += max(first, max(second, third)) * projectionWeights[projection];
}

float paper = 1.0 - saturate(ShadingStrength) * tone * surfaceMask;
float hatchCoverage = saturate(hatch * HatchStrength) * surfaceMask;
float graphite = lerp(paper, 0.11, hatchCoverage);
float edge = saturate(max(silhouetteEdge, max(normalEdge, depthEdge) * StructureStrength) * LineOpacity);
float displayGray = lerp(graphite, InkDarkness, edge);

// 遮罩视图是纯黑内圈、灰过渡、白外圈。原始视图直接返回最终场景输入，
// 不影响曝光计量，可与完全禁用效果的游戏画面交叉验证。
if (SketchDebugView > 2.5)
    return SceneColor.rgb;
if (SketchDebugView > 1.5)
    return sketchWeight.xxx;
if (SketchDebugView > 0.5)
    displayGray = 1.0 - tone * surfaceMask;

// NormalRadius 内权重严格为零，原始材质、颜色、光照和 TSR 结果直接通过。
// 色调映射之后输出的灰阶不会参与自动曝光，也不受 Film 曲线压缩。
return lerp(SceneColor.rgb, displayGray.xxx, sketchWeight);
