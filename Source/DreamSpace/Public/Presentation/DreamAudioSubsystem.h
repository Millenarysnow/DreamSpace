#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamAudioSubsystem.generated.h"

class UAudioComponent;
class USoundBase;

/**
 * 每个 GameInstance 一份的背景音乐管理器，主菜单切换地图时仍然存在。
 *
 * BGM 组件属于当前 World，结束该地图时明确停止并销毁；进入游戏音效属于音频设备，
 * 允许跨 OpenLevel 播放完，但退出 PIE / 关闭游戏时仍由本子系统清理。
 * 配置集中在 DefaultGame.ini，不依赖关卡里放置音频 Actor，也不需要更换 GameInstance 类。
 */
UCLASS(Config = Game, DefaultConfig)
class DREAMSPACE_API UDreamAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 由地图生命周期桥接器调用；只有配置的两张完整包路径会启动 BGM。 */
	void PlayMusicForWorld(UWorld* World);

	/** 只有待离开的 World 是当前音乐所属地图时才清理，避免旧地图迟到的回调停止新 BGM。 */
	void StopMusicForWorld(UWorld* World);

	/** 主菜单已通过地图有效性检查后调用；重复请求不会叠加音效或重复淡出。 */
	void BeginMenuTransition(UWorld* World, float FadeOutDuration);

	/** 供蓝图和实机验收读取真实组件；无音频设备 / 尚未播放时返回空。 */
	UFUNCTION(BlueprintPure, Category = "梦间|音频")
	UAudioComponent* GetMusicComponent() const { return MusicComponent; }

	UFUNCTION(BlueprintPure, Category = "梦间|音频")
	UAudioComponent* GetTransitionComponent() const { return TransitionComponent; }

	/** 使用完整软关卡引用匹配地图，既支持打包游戏，也能正确剥离 PIE 的 UEDPIE 前缀。 */
	UPROPERTY(Config, EditAnywhere, Category = "梦间|音频|地图")
	TSoftObjectPtr<UWorld> MenuMap;

	UPROPERTY(Config, EditAnywhere, Category = "梦间|音频|地图")
	TSoftObjectPtr<UWorld> GameplayMap;

	/** 两首音乐必须在 SoundWave 资源上启用循环；运行时不修改共享音频资源。 */
	UPROPERTY(Config, EditAnywhere, Category = "梦间|音频|资源")
	TSoftObjectPtr<USoundBase> MenuMusic;

	UPROPERTY(Config, EditAnywhere, Category = "梦间|音频|资源")
	TSoftObjectPtr<USoundBase> GameplayMusic;

	UPROPERTY(Config, EditAnywhere, Category = "梦间|音频|资源")
	TSoftObjectPtr<USoundBase> EnterGameSound;

	/** 线性音量倍率；先用温和的氛围音量，最终响度仍需结合实际扬声器 / 耳机验收。 */
	UPROPERTY(Config, EditAnywhere, Category = "梦间|音频|音量", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MenuMusicVolume = 0.6f;

	UPROPERTY(Config, EditAnywhere, Category = "梦间|音频|音量", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GameplayMusicVolume = 0.5f;

	UPROPERTY(Config, EditAnywhere, Category = "梦间|音频|音量", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EnterGameVolume = 1.0f;

	/** 菜单淡入与原画面淡入同步；游戏音乐进入时也缓慢抬起，避免突然插入。 */
	UPROPERTY(Config, EditAnywhere, Category = "梦间|音频|过渡", meta = (ClampMin = "0.0", Units = "s"))
	float MusicFadeInDuration = 1.0f;

protected:
	virtual void Deinitialize() override;

private:
	/** 强引用避免长音乐被 GC；销毁通过明确的地图 / GameInstance 生命周期完成。 */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MusicComponent;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> TransitionComponent;

	/** 菜单启动时预加载短音效，确认那一帧无需等待磁盘读取。 */
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> LoadedEnterGameSound;

	TWeakObjectPtr<UWorld> MusicWorld;
	bool bMenuTransitionStarted = false;

	bool CanPlayInWorld(const UWorld* World) const;
	static FString GetMapPackage(const UWorld* World);
	static USoundBase* LoadSound(const TSoftObjectPtr<USoundBase>& Reference, const TCHAR* Purpose);
	static void ReleaseComponent(TObjectPtr<UAudioComponent>& Component);

	/** 音效自然播完会自动销毁；先清掉本子系统引用，下一次开始请求才能创建新音效。 */
	UFUNCTION()
	void HandleTransitionFinished();
};

/**
 * 地图生命周期桥接：只在 Game / PIE 世界创建，在 World BeginPlay 时交给所属 GameInstance 播放。
 * 这样直接从 TEST 按 PIE、从菜单 OpenLevel、重复运行 PIE 都走同一条入口，
 * 与角色生成、GameMode 继承关系及场景内的蓝图 Actor 无关。
 */
UCLASS()
class DREAMSPACE_API UDreamMapAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
};
