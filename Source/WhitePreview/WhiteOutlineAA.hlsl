// 描边材质运行在色调映射之后，已经晚于引擎默认的 TSR / FXAA。
// 因此用第二个同阶段、较高优先级的后处理，在生成的墨线之后进行轻量抗锯齿。
// 在读取已完成的线稿时加入铅笔式小幅摆动，再沿局部边缘方向进行抗锯齿。
// 位移的是后处理图像的采样位置，原网格、相机以及深度/法线缓冲始终保持不动。
// 对已经生成的颜色做双线性采样，允许亚像素移动；直接移动点采样的深度则容易
// 一次跳动一个完整像素，并在墙角产生法线插值误差。
float2 viewportUV = GetViewportUV(Parameters);
float2 outputPixelUV = GetSceneTextureViewSize(PPI_PostProcessInput0).zw;
float2 strokePixel = viewportUV / outputPixelUV;
float2 strokePosition = strokePixel * (6.2831853 / max(StrokeScale, 24.0));

// 默认 AnimationTime=-1，使用 Time 节点的实时秒数；非负值可冻结为指定时间。
// 固定时间用于自动截图和比较，不依赖截图任务排队的实际耗时。
// 两个时间频率为 1 与 1/2，始终平滑且具有 2 / WobbleSpeed 秒的完整循环周期。
float seconds = AnimationTime < 0.0 ? TimeSeconds : AnimationTime;
float phase = seconds * max(WobbleSpeed, 0.0) * 6.2831853;
float2 wobble = float2(
    sin(dot(strokePosition, float2(0.37, 1.21)) + phase) * 0.55
        + sin(dot(strokePosition, float2(-0.47, 3.83)) - phase * 0.5) * 0.25
        + sin(dot(strokePosition, float2(0.73, 1.17))) * 0.20,
    sin(dot(strokePosition, float2(1.07, -0.29)) - phase + 0.9) * 0.55
        + sin(dot(strokePosition, float2(3.47, 0.71)) + phase * 0.5 + 2.3) * 0.25
        + sin(dot(strokePosition, float2(1.41, -0.59)) + 0.7) * 0.20);

// 默认每轴最多移动 0.9 个输出像素，不采用逐帧随机重画，保证长轮廓连续。
// 先以视口像素计算位移，再转换至实际缓冲 UV，兼容嵌入式编辑器视口与不同分辨率。
float2 displacedViewportUV = viewportUV + wobble * clamp(WobbleAmplitude, 0.0, 3.0) * outputPixelUV;
float2 uv = ClampSceneTextureUV(
    ViewportUVToSceneTextureUV(displacedViewportUV, PPI_PostProcessInput0), PPI_PostProcessInput0);
float2 pixel = GetSceneTextureBufferSize(PPI_PostProcessInput0).zw;
float3 lumaWeights = float3(0.299, 0.587, 0.114);
float3 center = SceneTextureLookup(uv, PPI_PostProcessInput0, true).rgb;
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

// 笔压是低频、连续的空间变化，颗粒是固定在屏幕上的细小石墨密度变化。
// 颗粒不使用时间随机数，因此不会整幅闪烁；只减轻现有笔迹，不向白纸添加噪点。
float pressure = 0.5 + 0.5 * (
    sin(dot(strokePosition, float2(0.83, 0.41)) + 1.3) * 0.65
        + sin(dot(strokePosition, float2(-1.17, 1.53))) * 0.35);
float grain = frac(sin(dot(floor(strokePixel), float2(127.1, 311.7))) * 43758.5453);
float graphiteDensity = clamp(1.0 - saturate(GraphiteSoftness)
    - saturate(PressureVariation) * pressure - saturate(GraphiteGrain) * grain, 0.55, 1.0);

// 默认白色面的灰度不低于 0.84；只有比它更暗的笔迹参与颗粒和笔压混合。
// 浓度下限保证线不会被噪声擦断，交叉处也不会叠成更黑的污块。
float pencilMask = 1.0 - smoothstep(0.45, 0.82, dot(smoothed, lumaWeights));
float3 graphiteColor = 1.0 - (1.0 - smoothed) * graphiteDensity;
return lerp(smoothed, graphiteColor, pencilMask);
