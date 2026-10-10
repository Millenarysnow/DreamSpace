// 此代码内嵌进菜单专用后处理材质；执行在描边与线稿抗锯齿之后。
// PaperSample 由普通 TextureSample 节点采样，UV 是当前视口 UV，不使用世界坐标。
// 纸纹静止在屏幕上，建筑仍使用自己的局部排线，模拟建筑画在一张纸上。
float grain = dot(PaperSample.rgb, float3(0.2126, 0.7152, 0.0722));
float3 paper = PaperTint.rgb * lerp(1.0, grain, saturate(PaperStrength));
// 乘法使纯白建筑与白色空背景自然融入同一纸面，黑色轮廓保持清楚。
// 不使用半透明覆盖图片，避免纸张的亮部把细小窗框和栏杆洗掉。
return SceneColor.rgb * paper;
