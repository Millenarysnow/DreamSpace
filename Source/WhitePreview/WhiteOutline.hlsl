// 此文件是 UE 后处理材质 Custom 节点的完整代码，由 generate_white_preview.py 嵌入。
// 不直接 include 外部文件，因此打包后的材质不依赖本机磁盘路径或编辑器 Python。
// 三个 SceneTexture 输入由生成脚本接入，用于向材质编译器声明所需的场景缓冲。

// Custom 节点不能在主函数内部直接定义普通 HLSL 函数；通过局部结构体封装笔划。
// 这里用程序化排线实现“按明暗累加笔划”的思路，不加载或复制参考图的纹理。
struct SurfaceHatching
{
    float Hash(float2 position)
    {
        return frac(sin(dot(position, float2(127.1, 311.7))) * 43758.5453);
    }

    float Noise(float2 position)
    {
        // 平滑插值的固定空间噪声：形状有手绘偏差，但不会逐帧随机改变。
        float2 cell = floor(position);
        float2 blend = frac(position);
        blend = blend * blend * (3.0 - 2.0 * blend);
        return lerp(lerp(Hash(cell), Hash(cell + float2(1, 0)), blend.x),
                    lerp(Hash(cell + float2(0, 1)), Hash(cell + float2(1, 1)), blend.x), blend.y);
    }

    float Stroke(float2 coordinate, float activation, float width, float irregularity, float strokeLength, float seed)
    {
        float row = floor(coordinate.y + 0.5);
        // 单条排线内部保留微小弯曲及笔压起伏；这不是轮廓或整幅图像的位移。
        // 每排略有不同的落笔偏移，再叠加两种平滑波长。偏移小于半个间距，
        // 同一排始终可识别，也不会形成完全等距的砖墙网格。
        float rowOffset = (Hash(float2(row, seed + 8.0)) * 2.0 - 1.0) * irregularity;
        float bend = rowOffset + (Noise(float2(coordinate.x * 0.32, row * 1.71 + seed)) * 2.0 - 1.0)
            * irregularity + sin(coordinate.x * 1.37 + row * 2.11 + seed) * irregularity * 0.25;
        float lane = coordinate.y + bend;
        float distance = abs(frac(lane + 0.5) - 0.5);
        float footprint = max(length(float2(ddx(coordinate.y), ddy(coordinate.y))), 0.0001);
        float pressure = lerp(0.78, 1.16, Noise(float2(coordinate.x * 0.6, row + seed)));
        float halfWidth = min(width * pressure * footprint * 0.5, 0.22);
        float coverage = saturate((halfWidth - distance) / footprint + 0.5);

        // 接笔位置在各排中错开，生成长短略有差别的手绘笔划，不形成规则竖向接缝。
        // 暗度提高时逐渐延长已有笔划；交叉层由调用方继续增加，避免整面同时黑化。
        float along = coordinate.x / max(strokeLength, 1.0) + Hash(float2(row, seed)) * 0.8;
        float segment = floor(along);
        float segmentPosition = frac(along);
        float segmentLength = lerp(0.42, 0.95, activation)
            * lerp(0.86, 1.0, Hash(float2(segment + seed, row)));
        float segmentAA = max(fwidth(along), 0.002);
        float endMask = smoothstep(0.0, segmentAA, segmentPosition)
            * (1.0 - smoothstep(segmentLength - segmentAA, segmentLength + segmentAA, segmentPosition));
        float graphite = lerp(0.78, 1.0, Noise(coordinate * float2(3.2, 7.0) + seed));
        float resolved = coverage * endMask * graphite;

        // 远处排线的屏幕间距小于约两像素时，逐渐转为该笔划的平均覆盖率。
        // 这样保留远处阴影明暗，避免密线产生闪烁摩尔纹，也不突然完全消失。
        float average = min(width * footprint, 0.44) * segmentLength * 0.88;
        return lerp(resolved, average, smoothstep(0.3, 0.65, footprint)) * activation;
    }
};
SurfaceHatching pencil;

// 预览关卡中的所有可见网格都是建筑；空背景的深度接近无穷大。
// 用深度限制生成主体遮罩，省去全项目 CustomDepth / Stencil 开关。
// MaxSubjectDepth 的单位为厘米，超过这个距离的像素输出纯白背景。
float centerDepth = max(DepthBuffer.r, 1.0);
float centerMask = 1.0 - step(MaxSubjectDepth, centerDepth);
float3 centerNormal = normalize(NormalBuffer.rgb + float3(0.00001, 0.0, 0.0));
float2 viewportUV = GetViewportUV(Parameters);

// 在色调映射之后描边。采样半径按最终输出像素换算，再转换为各场景缓冲的 UV：
// 编辑器视口可能只占纹理的一部分，TSR 的场景深度也可能低于最终分辨率。
// 不可把 PostProcessInput0 的纹理 UV 直接用于深度或法线，否则窗口缩放后会错位。
float2 outputPixelUV = GetSceneTextureViewSize(PPI_PostProcessInput0).zw;
// 手绘线不会沿整条边保持完全相同的宽度。使用连续、固定的空间波形模拟笔压，
// 而不是逐像素随机改变宽度，避免细长栏杆出现断线；轮廓始终保持固定。
// StrokeVariation=0 恢复原来的等宽描边，最大变化限制在 45% 以内。
float2 strokePixel = viewportUV / outputPixelUV;
float widthPressure = sin(dot(strokePixel, float2(0.093, 0.048))) * 0.6
    + sin(dot(strokePixel, float2(0.019, -0.077)) + 1.7) * 0.4;
float widthScale = 1.0 + clamp(StrokeVariation, 0.0, 0.45) * widthPressure;
float2 silhouetteStep = outputPixelUV * max(SilhouetteWidth * widthScale, 0.5);
float2 structureStep = outputPixelUV * max(StructureWidth * widthScale, 0.5);
float silhouetteEdge = 0.0;
float normalEdge = 0.0;
float depthEdge = 0.0;

// 四组相反方向共八个邻点，使斜边也能被识别，避免只取上下左右时出现缺口。
// 斜向单位向量归一化，保证横线与斜线使用相同的屏幕线宽。
const float2 directions[4] = {
    float2(1.0, 0.0), float2(0.0, 1.0),
    float2(0.70710678, 0.70710678), float2(0.70710678, -0.70710678)
};

[unroll]
for (int axis = 0; axis < 4; ++axis)
{
    float pairDepth[2];
    float pairMask[2];

    [unroll]
    for (int side = 0; side < 2; ++side)
    {
        float signValue = side == 0 ? -1.0 : 1.0;
        float2 offsetDirection = directions[axis] * signValue;
        float2 outerViewportUV = viewportUV + offsetDirection * silhouetteStep;
        float2 outerDepthUV = ClampSceneTextureUV(
            ViewportUVToSceneTextureUV(outerViewportUV, PPI_SceneDepth), PPI_SceneDepth);
        float outerDepth = SceneTextureLookup(outerDepthUV, PPI_SceneDepth, false).r;
        float outerMask = 1.0 - step(MaxSubjectDepth, outerDepth);
        silhouetteEdge = max(silhouetteEdge, abs(centerMask - outerMask));

        float2 innerViewportUV = viewportUV + offsetDirection * structureStep;
        float2 depthUV = ClampSceneTextureUV(
            ViewportUVToSceneTextureUV(innerViewportUV, PPI_SceneDepth), PPI_SceneDepth);
        float2 normalUV = ClampSceneTextureUV(
            ViewportUVToSceneTextureUV(innerViewportUV, PPI_WorldNormal), PPI_WorldNormal);
        float sampleDepth = max(SceneTextureLookup(depthUV, PPI_SceneDepth, false).r, 1.0);
        float sampleMask = 1.0 - step(MaxSubjectDepth, sampleDepth);
        float3 sampleNormal = normalize(SceneTextureLookup(normalUV, PPI_WorldNormal, false).rgb
            + float3(0.00001, 0.0, 0.0));
        pairDepth[side] = sampleDepth;
        pairMask[side] = sampleMask;

        // 只在两边都有建筑时比较法线。白色背景的 GBuffer 法线没有意义，
        // 不让它生成粗黑块。法线阈值保留墙角、台阶等结构，过滤曲面的细微变化。
        float normalDifference = 1.0 - saturate(dot(centerNormal, sampleNormal));
        float crease = smoothstep(NormalThreshold, NormalThreshold + 0.16, normalDifference);
        normalEdge = max(normalEdge, crease * centerMask * sampleMask);
    }

    // 透视投影下，同一平面的“倒数深度”沿屏幕直线近似线性。
    // 用相反邻点的二阶差分过滤倾斜墙面的正常深度梯度，只留下遮挡或台阶跳变；
    // 单纯使用相邻深度差会把倾斜的大墙误涂成黑色。
    float inverseDepthCurvature = abs(centerDepth / pairDepth[0]
        + centerDepth / pairDepth[1] - 2.0);
    float discontinuity = smoothstep(DepthThreshold, DepthThreshold * 2.0, inverseDepthCurvature);

    // 遮挡交界仅在靠前的表面画线，减少前后两侧叠加产生的粗边。
    float isNearSide = step(centerDepth, max(pairDepth[0], pairDepth[1]));
    depthEdge = max(depthEdge, discontinuity * centerMask * pairMask[0] * pairMask[1] * isNearSide);
}

// 用真实光照、投影阴影、接触暗部及法线受光构造排线的明暗依据。
// 场景本身使用统一白色材质，因此亮度差主要来自光照而非原贴图的花纹。
// 这不是后处理对阴影缓冲的直接访问；是将已渲染的白色表面亮度转成排线密度。
float3 lightDirection = normalize(LightDirection.rgb);
float geometryShade = 1.0 - saturate(dot(centerNormal, lightDirection));
float sceneLuminance = saturate(dot(SceneColor.rgb, float3(0.2126, 0.7152, 0.0722)));
float sceneShade = 1.0 - smoothstep(0.08, max(ShadowReference, 0.1), sceneLuminance);
float tone = saturate((lerp(geometryShade, sceneShade, saturate(ShadowInfluence))
    - HatchToneBias) * max(HatchToneContrast, 0.01));

// 先出现稀疏单向排线，然后增加交叉层，最暗处再插入更密的同向笔划。
// 各层连续淡入，并复用固定坐标；明暗改变不会重新随机分布全部笔划。
// 受光中间调也保留少量长笔划；不把所有亮墙完全清空，阴影再加交叉层。
float primaryActivation = smoothstep(0.08, 0.32, tone);
float crossActivation = smoothstep(0.43, 0.85, tone);
// 最密层只用于最深暗部；普通背光墙面保留可分辨的线间留白，避免看成噪点。
float denseActivation = smoothstep(0.87, 1.0, tone);
float3 projectionWeights = pow(abs(centerNormal), 8.0);
projectionWeights /= max(dot(projectionWeights, float3(1, 1, 1)), 0.0001);
float3 surface = SurfacePosition * centerMask / max(HatchSpacing, 1.0);
float angle = HatchAngle * 0.017453293;
float2 primaryAlong = float2(cos(angle), sin(angle));
float2 primaryAcross = float2(-sin(angle), cos(angle));
// 交叉层与主层相差 60 度；斜交叠加比接近直角的格栅更接近铅笔阴影。
float crossAngle = angle + 1.04719755;
float2 crossAlong = float2(cos(crossAngle), sin(crossAngle));
float2 crossAcross = float2(-sin(crossAngle), cos(crossAngle));
float hatch = 0.0;

// 在三个世界平面上投影，按几何法线混合；墙面与顶面的排线方向自然随面转折。
// 笔划锚定于可见表面的世界位置，观察视角改变时仍贴在原处，不是屏幕网纹。
[unroll]
for (int projection = 0; projection < 3; ++projection)
{
    float2 projected = projection == 0 ? surface.yz : (projection == 1 ? surface.xz : surface.xy);
    float2 primary = float2(dot(projected, primaryAlong), dot(projected, primaryAcross));
    // 次层间距稍大，防止两个方向都达到满密度时变成同样粗重的网格。
    float2 cross = float2(dot(projected, crossAlong), dot(projected, crossAcross)) / 1.18;
    float seed = 5.37 + projection * 13.1;
    float first = pencil.Stroke(primary, primaryActivation, HatchWidth,
                               HatchIrregularity, HatchStrokeLength, seed);
    float second = pencil.Stroke(cross, crossActivation, HatchWidth,
                                HatchIrregularity, HatchStrokeLength * 0.83, seed + 2.1);
    float third = pencil.Stroke(primary + float2(0.37, 0.5), denseActivation, HatchWidth * 0.9,
                               HatchIrregularity, HatchStrokeLength, seed + 4.3);
    // 取笔划覆盖的并集，避免相交处重复相乘出现额外黑斑。
    float layers = max(first, max(second, third));
    hatch += layers * projectionWeights[projection];
}

// 少量浅灰底色只辅助空间关系，主要暗度由白纸上的石墨笔划承担。
float shade = saturate(ShadingStrength) * tone;
float3 paperColor = lerp(float3(1.0, 1.0, 1.0), float3(1.0 - shade, 1.0 - shade, 1.0 - shade), centerMask);
float hatchCoverage = saturate(hatch * HatchStrength) * centerMask;
float3 hatchedPaper = lerp(paperColor, float3(0.11, 0.11, 0.11), hatchCoverage);

// 调试视图帮助检查排线确实追随明暗。正常使用为 0，不修改源关卡或原材料。
if (HatchDebugView > 2.5)
{
    return lerp(float3(1, 1, 1), SceneColor.rgb, centerMask);
}
if (HatchDebugView > 1.5)
{
    return lerp(float3(1, 1, 1), float3(0.11, 0.11, 0.11), hatchCoverage);
}
if (HatchDebugView > 0.5)
{
    return float3(1, 1, 1) * (1.0 - tone * centerMask);
}

// 先合并边缘再混合墨色：交叉处不会因为重复相乘而变成过大的黑块。
// 外轮廓使用完整墨色，内部结构线通过独立强度保持轻重层次。
float structureEdge = max(normalEdge, depthEdge) * saturate(StructureStrength);
float inkCoverage = saturate(max(silhouetteEdge, structureEdge) * LineOpacity);
return lerp(hatchedPaper, InkColor.rgb, inkCoverage);
