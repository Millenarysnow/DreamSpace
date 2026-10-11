// 游戏用平滑的距离限制：只平滑新增墨线，玩家内圈直接通过原始 TSR 结果。
// 用与主材质相同的去抖深度 / 逆投影重建球形遮罩，不能复用默认取整的
// WorldPosition；两阶段的边界必须一致，防止平滑影响正常渲染的区域。
float2 viewportUV = GetViewportUV(Parameters);
// 生成时嵌入与主线稿完全相同的逐 texel 距离判断，不能复制另一套平均深度公式。
// PROXIMITY_DISTANCE_MASK
// 调试视图要保留精确的遮罩 / 原始颜色，内圈也不执行额外颜色滤波。
if (SketchDebugView > 0.5 || sketchWeight <= 0.0)
    return SceneColor.rgb;

// 生成器在此嵌入已验收的 WhiteOutlineAA.hlsl，将最后的返回按距离权重混合。
// 整段代码存入材质 Custom 节点，打包运行无需依赖外部文件。
// PROXIMITY_AA_IMPLEMENTATION
