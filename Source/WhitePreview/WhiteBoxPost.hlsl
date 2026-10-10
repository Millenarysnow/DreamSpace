// 干净白盒后处理：仅把已经受光、投影并完成抗锯齿的场景颜色映射为浅灰到白色。
// 墙面的明暗来自真实光照，不再搜索深度／法线边界，也不为窗框和栏杆添加灰色晕圈。
// 白色环境网格与建筑一起经过引擎的抗锯齿；此处无需用深度重新裁出硬边轮廓，
// 因而能保留细小结构与背景之间的平滑过渡，避免色调映射后读取 GBuffer 导致的毛边。
// 代码由生成脚本嵌入 Custom 节点，运行及打包后不依赖外部 HLSL 文件。

float sceneGray = max(dot(SceneColor.rgb, float3(0.2126, 0.7152, 0.0722)), 0.0);

// WhitePoint 决定何种场景亮度开始显示为纯白；背景的亮度高于这个阈值。
// MidtoneLift 小于 1 时抬亮中间调，同时保留真正的遮挡阴影和不同朝向的块面。
// ShadowFloor 是最深阴影的灰度下限，防止无天空光的门洞或背面变成厚重黑块。
float whiteLevel = saturate(sceneGray / max(WhitePoint, 0.01));
float lifted = pow(whiteLevel, max(MidtoneLift, 0.01));
float finalGray = lerp(saturate(ShadowFloor), 1.0, lifted);
return float3(finalGray, finalGray, finalGray);
