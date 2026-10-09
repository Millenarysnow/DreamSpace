// 白盒后处理：只保留黑白灰的形体明暗，不生成单独的黑色描边。
// 法线决定墙面朝向的基础亮度，真实场景亮度保留遮挡、门洞和接触阴影。
// 可见面的边界额外带一圈柔和灰色，并按照距边界的距离连续衰减到面中心。
// 代码由生成脚本嵌入 Custom 节点，运行及打包后不依赖外部 HLSL 文件。

float centerDepth = max(DepthBuffer.r, 1.0);
if (centerDepth >= MaxSubjectDepth)
{
    // 空世界没有有效的法线和阴影；直接输出白色，避免在背景生成灰色光晕。
    return float3(1.0, 1.0, 1.0);
}

float3 centerNormal = normalize(NormalBuffer.rgb + float3(0.00001, 0.0, 0.0));
float2 viewportUV = GetViewportUV(Parameters);
float2 outputSize = GetSceneTextureViewSize(PPI_PostProcessInput0).xy;
float2 outputPixel = rcp(outputSize);

// 透视投影下，同一平面的倒数深度沿屏幕直线线性变化。
// 先用相反邻点估计这个变化率，后续大范围采样减去正常透视变化，
// 避免把斜墙的深度渐变误当成边界，导致整面被压暗。
const float2 directions[8] = {
    float2(1.0, 0.0), float2(-1.0, 0.0),
    float2(0.0, 1.0), float2(0.0, -1.0),
    float2(0.70710678, 0.70710678), float2(-0.70710678, -0.70710678),
    float2(0.70710678, -0.70710678), float2(-0.70710678, 0.70710678)
};
float ratios[4];
float valid[4];
[unroll]
for (int side = 0; side < 4; ++side)
{
    float2 sampleUV = viewportUV + directions[side] * outputPixel;
    float depth = max(SceneTextureLookup(ClampSceneTextureUV(
        ViewportUVToSceneTextureUV(sampleUV, PPI_SceneDepth), PPI_SceneDepth),
        PPI_SceneDepth, false).r, 1.0);
    float3 normal = normalize(SceneTextureLookup(ClampSceneTextureUV(
        ViewportUVToSceneTextureUV(sampleUV, PPI_WorldNormal), PPI_WorldNormal),
        PPI_WorldNormal, false).rgb + float3(0.00001, 0.0, 0.0));
    ratios[side] = centerDepth / depth;
    // 靠近真实墙角时只用仍在同一平面上的那一侧估计梯度。
    valid[side] = (depth < MaxSubjectDepth && dot(normal, centerNormal) > 0.98
        && abs(ratios[side] - 1.0) < DepthThreshold * 2.0) ? 1.0 : 0.0;
}
float2 inverseSlope = float2(
    ((ratios[0] - 1.0) * valid[0] + (1.0 - ratios[1]) * valid[1])
        / max(valid[0] + valid[1], 1.0),
    ((ratios[2] - 1.0) * valid[2] + (1.0 - ratios[3]) * valid[3])
        / max(valid[2] + valid[3], 1.0));

// Custom 节点内使用局部结构体封装缓冲采样。每次调用只判断一个位置是否
// 离开当前可见面；没有硬线遮罩，也不按网格三角形生成线框。
struct WhiteBoxBoundarySampler
{
    float boundary(float2 uv, float2 offset, float depth, float3 normal,
        float2 slope, float normalThreshold, float depthThreshold, float maxDepth)
    {
        float sampleDepth = max(SceneTextureLookup(ClampSceneTextureUV(
            ViewportUVToSceneTextureUV(uv, PPI_SceneDepth), PPI_SceneDepth),
            PPI_SceneDepth, false).r, 1.0);
        if (sampleDepth >= maxDepth)
        {
            return 1.0;
        }
        float3 sampleNormal = normalize(SceneTextureLookup(ClampSceneTextureUV(
            ViewportUVToSceneTextureUV(uv, PPI_WorldNormal), PPI_WorldNormal),
            PPI_WorldNormal, false).rgb + float3(0.00001, 0.0, 0.0));
        float normalDifference = 1.0 - saturate(dot(normal, sampleNormal));
        float expectedRatio = max(1.0 + dot(slope, offset), 0.01);
        float depthResidual = abs(depth / sampleDepth - expectedRatio) / expectedRatio;
        return max(smoothstep(normalThreshold, normalThreshold + 0.12, normalDifference),
            smoothstep(depthThreshold, depthThreshold * 3.0, depthResidual));
    }
};
WhiteBoxBoundarySampler sampler;

// EdgeWidth 表示 1080p 下的渐变宽度。随输出高度等比例换算，
// 窗口尺寸改变时保持相同的视觉比例，不使用内部 GBuffer 分辨率作为线宽。
float radius = max(EdgeWidth, 1.0) * outputSize.y / 1080.0;
float nearestBoundary = radius;
const float distances[6] = {0.025, 0.07, 0.16, 0.33, 0.65, 1.0};
[loop]
for (int directionIndex = 0; directionIndex < 8; ++directionIndex)
{
    float previousDistance = 0.0;
    [loop]
    for (int sampleIndex = 0; sampleIndex < 6; ++sampleIndex)
    {
        // 已找到近边界后，其他方向只需搜索到这个距离，减少无意义的远处采样。
        float sampleDistance = min(radius * distances[sampleIndex], nearestBoundary);
        float2 offset = directions[directionIndex] * sampleDistance;
        float crossed = sampler.boundary(viewportUV + offset * outputPixel, offset,
            centerDepth, centerNormal, inverseSlope, NormalThreshold, DepthThreshold, MaxSubjectDepth);
        if (crossed > 0.5)
        {
            // 在最后一个同面点与第一个异面点之间细分三次。
            // 用真实距离计算渐变，避免固定近／中／远权重产生一圈圈色阶。
            float low = previousDistance;
            float high = sampleDistance;
            [unroll]
            for (int refinement = 0; refinement < 3; ++refinement)
            {
                float middle = (low + high) * 0.5;
                float2 middleOffset = directions[directionIndex] * middle;
                float middleCrossed = sampler.boundary(viewportUV + middleOffset * outputPixel,
                    middleOffset, centerDepth, centerNormal, inverseSlope,
                    NormalThreshold, DepthThreshold, MaxSubjectDepth);
                if (middleCrossed > 0.5)
                {
                    high = middle;
                }
                else
                {
                    low = middle;
                }
            }
            nearestBoundary = min(nearestBoundary, (low + high) * 0.5);
            break;
        }
        if (sampleDistance >= nearestBoundary)
        {
            break;
        }
        previousDistance = sampleDistance;
    }
}

// 不同朝向的面具有稳定的灰度差。少量真实阴影让走廊、窗框和内凹结构
// 更易读；所有颜色均压成灰度，最终 R/G/B 始终一致。
float sceneGray = saturate(dot(SceneColor.rgb, float3(0.2126, 0.7152, 0.0722)));
float normalLight = saturate(dot(centerNormal, normalize(LightDirection.rgb)) * 0.5 + 0.5);
float faceGray = saturate(1.0 - (1.0 - FaceBase) * (1.0 - normalLight)
    - FaceContrast * (1.0 - sceneGray));
float edgeProfile = pow(1.0 - smoothstep(0.0, radius, nearestBoundary), max(EdgeFalloff, 0.1));
float finalGray = lerp(faceGray, min(EdgeGray, faceGray), edgeProfile * saturate(EdgeDarkness));
return float3(finalGray, finalGray, finalGray);
