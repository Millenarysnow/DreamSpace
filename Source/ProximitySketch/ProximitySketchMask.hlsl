// 两个后处理通道共用的三维距离遮罩；由生成器内嵌到各自的 Custom 节点中。
// 不能先混合栏杆与背后墙面的深度，再对“平均深度”计算距离：那会得到
// 一个并不存在的表面，使细栏杆在过渡带中突然被归到另一侧，造成局部丢失。
// 正确顺序是每个真实深度 texel 独立逆投影、计算距离权重，最后插值覆盖率。
struct ProximityDistanceMask
{
    float Weight(float2 viewportUV, float3 playerPosition, float radius,
        float transition, float maximumDepth, float strength)
    {
        float4 size = GetSceneTextureViewSize(PPI_SceneDepth);
        float2 jitteredUV = viewportUV + ResolvedView.TemporalAAParams.zw * size.zw;
        float2 pixel = jitteredUV * size.xy - 0.5;
        float2 base = floor(pixel);
        float2 blend = frac(pixel);
        float result = 0.0;
        [unroll]
        for (int tap = 0; tap < 4; ++tap)
        {
            float2 corner = float2(tap & 1, tap >> 1);
            float2 tapUV = (base + corner + 0.5) * size.zw;
            float2 sceneUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(tapUV,
                PPI_SceneDepth), PPI_SceneDepth);
            float depth = max(SceneTextureLookup(sceneUV, PPI_SceneDepth, false).r, 1.0);
            float customDepth = SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(tapUV,
                PPI_CustomDepth), PPI_CustomDepth), PPI_CustomDepth, false).r;
            float stencil = SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(tapUV,
                PPI_CustomStencil), PPI_CustomStencil), PPI_CustomStencil, false).r;
            // 241 只属于手办可见的透明像素；被不透明建筑遮住时仍采用建筑深度。
            if (abs(stencil - 241.0) < 0.5 && customDepth < depth)
                depth = max(customDepth, 1.0);
            float tapWeight = 1.0;
            if (depth < maximumDepth)
            {
                // 逆投影坐标必须属于当前 tap，不能拿中心像素的射线还原邻居的深度。
                float2 bufferPixel = sceneUV * GetSceneTextureBufferSize(PPI_SceneDepth).xy;
                float3 position = DFHackToFloat(SvPositionToWorld(float4(bufferPixel,
                    ConvertToDeviceZ(depth), 1)));
                tapWeight = smoothstep(max(radius, 0.0), max(radius, 0.0) + max(transition, 0.01),
                    length(position - playerPosition));
            }
            float coverage = (corner.x > 0.5 ? blend.x : 1.0 - blend.x)
                * (corner.y > 0.5 ? blend.y : 1.0 - blend.y);
            result += coverage * tapWeight;
        }
        // 全部 tap 都在内圈时严格为 0，主通道和平滑通道都直接保留原始 TSR 图像。
        return result * saturate(strength);
    }
};
ProximityDistanceMask proximityMask;
float sketchWeight = proximityMask.Weight(viewportUV, PlayerPosition.rgb,
    NormalRadius, TransitionWidth, MaxSubjectDepth, EffectStrength);
