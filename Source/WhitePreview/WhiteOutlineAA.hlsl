// 描边材质运行在色调映射之后，已经晚于引擎默认的 TSR / FXAA。
// 因此用第二个同阶段、较高优先级的后处理，在生成的墨线之后进行轻量抗锯齿。
// 本阶段只平滑已经生成的轮廓和阴影排线，不进行时间位移或额外颗粒混合。
// 手绘明暗由前一阶段贴在建筑表面的笔划产生，白色留白保持纯白。
float2 uv = GetDefaultSceneTextureUV(Parameters, PPI_PostProcessInput0);
float2 pixel = GetSceneTextureBufferSize(PPI_PostProcessInput0).zw;
float3 lumaWeights = float3(0.299, 0.587, 0.114);
float3 center = SceneColor.rgb;
float3 northwest = SceneTextureLookup(ClampSceneTextureUV(uv + float2(-1, -1) * pixel,
    PPI_PostProcessInput0), PPI_PostProcessInput0, true).rgb;
float3 northeast = SceneTextureLookup(ClampSceneTextureUV(uv + float2(1, -1) * pixel,
    PPI_PostProcessInput0), PPI_PostProcessInput0, true).rgb;
float3 southwest = SceneTextureLookup(ClampSceneTextureUV(uv + float2(-1, 1) * pixel,
    PPI_PostProcessInput0), PPI_PostProcessInput0, true).rgb;
float3 southeast = SceneTextureLookup(ClampSceneTextureUV(uv + float2(1, 1) * pixel,
    PPI_PostProcessInput0), PPI_PostProcessInput0, true).rgb;

float lumaCenter = dot(center, lumaWeights);
float lumaNW = dot(northwest, lumaWeights);
float lumaNE = dot(northeast, lumaWeights);
float lumaSW = dot(southwest, lumaWeights);
float lumaSE = dot(southeast, lumaWeights);
float lumaMin = min(lumaCenter, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
float lumaMax = max(lumaCenter, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

// 浅灰平面和白背景的局部对比很低，保持原值；只平滑高对比的黑线边缘。
float3 smoothed = center;
if (lumaMax - lumaMin >= max(0.025, lumaMax * 0.1))
{
    float2 direction = float2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)),
        (lumaNW + lumaSW) - (lumaNE + lumaSE));
    float directionReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 0.0078125);
    float inverseMinimum = rcp(min(abs(direction.x), abs(direction.y)) + directionReduce);
    direction = clamp(direction * inverseMinimum, -4.0, 4.0) * pixel;

    // 短范围结果保留细节，长范围结果更平滑；亮度越界时退回短范围，
    // 避免在细黑线旁插值出光晕或将小窗框抹成一团。
    float3 shortResult = 0.5 * (
        SceneTextureLookup(ClampSceneTextureUV(uv - direction / 6.0, PPI_PostProcessInput0),
            PPI_PostProcessInput0, true).rgb
        + SceneTextureLookup(ClampSceneTextureUV(uv + direction / 6.0, PPI_PostProcessInput0),
            PPI_PostProcessInput0, true).rgb);
    float3 longResult = shortResult * 0.5 + 0.25 * (
        SceneTextureLookup(ClampSceneTextureUV(uv - direction * 0.5, PPI_PostProcessInput0),
            PPI_PostProcessInput0, true).rgb
        + SceneTextureLookup(ClampSceneTextureUV(uv + direction * 0.5, PPI_PostProcessInput0),
            PPI_PostProcessInput0, true).rgb);
    float lumaLong = dot(longResult, lumaWeights);
    float3 edgeSmoothed = (lumaLong < lumaMin || lumaLong > lumaMax) ? shortResult : longResult;
    smoothed = lerp(center, edgeSmoothed, saturate(SmoothingStrength));
}

return smoothed;
