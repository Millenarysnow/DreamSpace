#include "DreamAudioSubsystem.h"

#include "DreamSpace.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

bool UDreamAudioSubsystem::CanPlayInWorld(const UWorld* World) const
{
	// 编辑器预览、专用服务器和 -nosound 验收不应尝试创建音源，也不把正常静音记成资源错误。
	// GameInstance 身份检查同时隔离多窗口 PIE，防止另一场游戏的地图事件干扰当前声音。
	return World && World->IsGameWorld() && World->GetGameInstance() == GetGameInstance()
		&& !World->IsNetMode(NM_DedicatedServer) && World->bAllowAudioPlayback
		&& GEngine && GEngine->UseSound();
}

FString UDreamAudioSubsystem::GetMapPackage(const UWorld* World)
{
	// PIE 在完整包名中插入 UEDPIE_0_ 等前缀；不能只比较短名字，以免其他目录下同名 TEST 误播放。
	return World ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : FString();
}

USoundBase* UDreamAudioSubsystem::LoadSound(const TSoftObjectPtr<USoundBase>& Reference, const TCHAR* Purpose)
{
	USoundBase* Sound = Reference.LoadSynchronous();
	if (!Sound)
	{
		// 缺少音频不会改变菜单输入或切关流程，但日志明确告诉开发者要检查哪个导入资源。
		UE_LOG(LogDreamSpace, Error, TEXT("[音频] 无法加载%s：%s"), Purpose, *Reference.ToString());
	}
	return Sound;
}

void UDreamAudioSubsystem::ReleaseComponent(TObjectPtr<UAudioComponent>& Component)
{
	UAudioComponent* Previous = Component;
	Component = nullptr;
	if (IsValid(Previous))
	{
		// Stop 可能同步广播 OnAudioFinished；先解除回调，避免清理过程中重入并修改新组件引用。
		Previous->OnAudioFinished.Clear();
		Previous->Stop();
		Previous->DestroyComponent();
	}
}

void UDreamAudioSubsystem::PlayMusicForWorld(UWorld* World)
{
	if (!CanPlayInWorld(World) || (MusicWorld.Get() == World && IsValid(MusicComponent)))
		return;

	ReleaseComponent(MusicComponent);
	MusicWorld.Reset();
	bMenuTransitionStarted = false;
	const FString Package = GetMapPackage(World);
	const bool bMenu = Package == MenuMap.ToSoftObjectPath().GetLongPackageName();
	const bool bGameplay = Package == GameplayMap.ToSoftObjectPath().GetLongPackageName();
	if (!bMenu && !bGameplay)
	{
		// 其他玩法示例 / 美术预览地图没有配置 BGM，进入它们时结束遗留过场音效。
		ReleaseComponent(TransitionComponent);
		LoadedEnterGameSound = nullptr;
		return;
	}

	if (bMenu)
	{
		ReleaseComponent(TransitionComponent);
		LoadedEnterGameSound = LoadSound(EnterGameSound, TEXT("进入游戏音效"));
	}
	USoundBase* Sound = LoadSound(bMenu ? MenuMusic : GameplayMusic, bMenu ? TEXT("主菜单音乐") : TEXT("游戏音乐"));
	if (!Sound)
		return;

	// BGM 不跨地图：组件注册到当前 World；下一张地图开始前停止旧音乐，而短过场音效单独持久化。
	// CreateSound2D 固定使用 2D 声音，不受相机距离、房间遮挡或角色重力旋转的空间衰减影响。
	MusicComponent = UGameplayStatics::CreateSound2D(World, Sound, 1.0f, 1.0f, 0.0f, nullptr, false, false);
	if (!MusicComponent)
	{
		UE_LOG(LogDreamSpace, Warning, TEXT("[音频] 当前没有可用播放设备，跳过 %s 的 BGM。"), *Package);
		return;
	}
	MusicWorld = World;
	MusicComponent->FadeIn(FMath::Max(MusicFadeInDuration, 0.0f),
		FMath::Clamp(bMenu ? MenuMusicVolume : GameplayMusicVolume, 0.0f, 1.0f));
	UE_LOG(LogDreamSpace, Log, TEXT("[音频] 开始循环音乐：%s，地图：%s"), *Sound->GetPathName(), *Package);
}

void UDreamAudioSubsystem::StopMusicForWorld(UWorld* World)
{
	if (MusicWorld.Get() != World)
		return;
	ReleaseComponent(MusicComponent);
	MusicWorld.Reset();
	// OpenLevel 会结束旧 World；这里保留设备级过场音效，让它继续播放到自然结束。
}

void UDreamAudioSubsystem::BeginMenuTransition(UWorld* World, float FadeOutDuration)
{
	if (bMenuTransitionStarted || !CanPlayInWorld(World)
		|| GetMapPackage(World) != MenuMap.ToSoftObjectPath().GetLongPackageName())
		return;
	bMenuTransitionStarted = true;

	if (MusicWorld.Get() == World && IsValid(MusicComponent))
	{
		// 与画面共用淡出时长，零时长配置直接停止，既不会突兀恢复，也不会依赖 Tick 手动调音量。
		if (FadeOutDuration > 0.0f)
			MusicComponent->FadeOut(FadeOutDuration, 0.0f);
		else
			ReleaseComponent(MusicComponent);
	}

	if (!LoadedEnterGameSound)
		LoadedEnterGameSound = LoadSound(EnterGameSound, TEXT("进入游戏音效"));
	if (!LoadedEnterGameSound)
		return;
	ReleaseComponent(TransitionComponent);
	TransitionComponent = UGameplayStatics::CreateSound2D(World, LoadedEnterGameSound,
		FMath::Clamp(EnterGameVolume, 0.0f, 1.0f), 1.0f, 0.0f, nullptr, true, true);
	if (TransitionComponent)
	{
		// 持久化组件属于音频设备，bIgnoreForFlushing 由 CreateSound2D 设置。
		// 地图同步加载 / Flush 时继续播放；先安装自然结束回调，再 Play，兼容非常短的音效。
		TransitionComponent->OnAudioFinished.AddDynamic(this, &UDreamAudioSubsystem::HandleTransitionFinished);
		TransitionComponent->Play();
		UE_LOG(LogDreamSpace, Log, TEXT("[音频] 播放一次进入游戏音效：%s"), *LoadedEnterGameSound->GetPathName());
	}
}

void UDreamAudioSubsystem::HandleTransitionFinished()
{
	TransitionComponent = nullptr;
	UE_LOG(LogDreamSpace, Log, TEXT("[音频] 进入游戏音效自然播放完毕。"));
}

void UDreamAudioSubsystem::Deinitialize()
{
	// GameInstance 的结束覆盖停止 PIE、关闭游戏和异常中断切关；设备级音效不能遗留到下一场 PIE。
	ReleaseComponent(TransitionComponent);
	ReleaseComponent(MusicComponent);
	LoadedEnterGameSound = nullptr;
	MusicWorld.Reset();
	bMenuTransitionStarted = false;
	Super::Deinitialize();
}

bool UDreamMapAudioSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDreamMapAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UGameInstance* Instance = InWorld.GetGameInstance())
		if (UDreamAudioSubsystem* Audio = Instance->GetSubsystem<UDreamAudioSubsystem>())
			Audio->PlayMusicForWorld(&InWorld);
}

void UDreamMapAudioSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	if (UGameInstance* Instance = InWorld.GetGameInstance())
		if (UDreamAudioSubsystem* Audio = Instance->GetSubsystem<UDreamAudioSubsystem>())
			Audio->StopMusicForWorld(&InWorld);
	Super::OnWorldEndPlay(InWorld);
}

void UDreamMapAudioSubsystem::Deinitialize()
{
	// 某些编辑器中止路径可能直接清理 World；额外幂等清理保证不用依赖所有 Actor 都正常 EndPlay。
	if (UWorld* World = GetWorld())
		if (UGameInstance* Instance = World->GetGameInstance())
			if (UDreamAudioSubsystem* Audio = Instance->GetSubsystem<UDreamAudioSubsystem>())
				Audio->StopMusicForWorld(World);
	Super::Deinitialize();
}
