#include "DreamPivotPointComponent.h"
#include "DreamPuzzleDebug.h"
#include "DrawDebugHelpers.h"

UDreamPivotPointComponent::UDreamPivotPointComponent()
{
	// 调试绘制依赖 Tick；枢轴点本身没有任何每帧逻辑，开销可以忽略。
	PrimaryComponentTick.bCanEverTick = true;
}

void UDreamPivotPointComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 调试开关打开时逐帧绘制；DrawDebug 系列绘制的图形只保留一帧，关闭开关后立刻消失。
	if (DreamPuzzleDebug::IsPivotDebugDrawEnabled())
		DrawDebugVisualization();
}

void UDreamPivotPointComponent::DrawDebugVisualization() const
{
	const UWorld* World = GetWorld();
	if (!World)
		return;

	// 枢轴点位置：黄色球标记，便于在复杂场景中一眼定位。
	DrawDebugSphere(World, GetComponentLocation(), DebugMarkerRadius, 12, FColor::Yellow, false, -1.0f, 0, 2.0f);

	// 枢轴点局部坐标系：X 红、Y 绿、Z 蓝，与编辑器视口的坐标轴配色一致。
	// 这三根轴就是其他交互组件可选的转动/运动轴。
	const FVector Origin = GetComponentLocation();
	const FQuat Rotation = GetComponentQuat();
	DrawDebugDirectionalArrow(World, Origin, Origin + Rotation.GetAxisX() * DebugAxisLength, 15.0f, FColor::Red,
		false, -1.0f, 0, 2.0f);
	DrawDebugDirectionalArrow(World, Origin, Origin + Rotation.GetAxisY() * DebugAxisLength, 15.0f, FColor::Green,
		false, -1.0f, 0, 2.0f);
	DrawDebugDirectionalArrow(World, Origin, Origin + Rotation.GetAxisZ() * DebugAxisLength, 15.0f, FColor::Blue,
		false, -1.0f, 0, 2.0f);
}
