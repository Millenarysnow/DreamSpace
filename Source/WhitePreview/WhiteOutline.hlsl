// 此文件是 UE 后处理材质 Custom 节点的完整代码，由 generate_white_preview.py 嵌入。
// 不直接 include 外部文件，因此打包后的材质不依赖本机磁盘路径或编辑器 Python。
// 三个 SceneTexture 输入由生成脚本接入，用于向材质编译器声明所需的场景缓冲。

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
float2 silhouetteStep = outputPixelUV * max(SilhouetteWidth, 0.5);
float2 structureStep = outputPixelUV * max(StructureWidth, 0.5);
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

// 白色表面保留少量形体明暗：主要由几何法线产生稳定的浅灰，
// 少量真实场景亮度保留接触关系。所有原贴图颜色已由白色网格材质替换。
// ShadingStrength=0 时得到严格纯白表面，默认值则让建筑在白背景中更易读。
float3 lightDirection = normalize(LightDirection.rgb);
float geometryShade = 1.0 - saturate(dot(centerNormal, lightDirection) * 0.5 + 0.5);
float sceneLuminance = saturate(dot(SceneColor.rgb, float3(0.2126, 0.7152, 0.0722)));
float shade = saturate(ShadingStrength) * (geometryShade * 0.8 + (1.0 - sceneLuminance) * 0.2);
float3 paperColor = lerp(float3(1.0, 1.0, 1.0), float3(1.0 - shade, 1.0 - shade, 1.0 - shade), centerMask);

// 先合并边缘再混合墨色：交叉处不会因为重复相乘而变成过大的黑块。
// 外轮廓使用完整墨色，内部结构线通过独立强度保持轻重层次。
float structureEdge = max(normalEdge, depthEdge) * saturate(StructureStrength);
float inkCoverage = saturate(max(silhouetteEdge, structureEdge) * LineOpacity);
return lerp(paperColor, InkColor.rgb, inkCoverage);
