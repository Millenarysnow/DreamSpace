#include "DreamPlayerController.h"

#include "DreamCharacter.h"
#include "DreamHUD.h"
#include "DreamPasswordChest.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "InputKeyEventArgs.h"
#include "UnrealClient.h"

bool ADreamPlayerController::BeginPasswordEntry(ADreamPasswordChest* Chest)
{
	ADreamCharacter* ControlledCharacter = Cast<ADreamCharacter>(GetPawn());
	if (IsEnteringPassword() || !IsLocalController() || !IsValid(Chest) || !Chest->CanInteract(ControlledCharacter) ||
		Chest->GetChestState() != EDreamPasswordChestState::Locked || !Cast<ADreamHUD>(GetHUD()))
		return false;
	ActivePasswordChest = Chest;
	PasswordPawn = ControlledCharacter;
	EnteredPassword.Reset();
	PasswordEntryMessage.Reset();
	// 只添加自己的一次忽略计数，不清空其它系统的锁；关闭时按相同次数释放。
	bPasswordInputLocked = true;
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	ControlledCharacter->StopJumping();
	// 本面板使用键盘输入和现有 HUD，不切换 ViewTarget/InputMode，避免破坏第三人称鼠标捕获。
	return true;
}

void ADreamPlayerController::ClosePasswordEntry()
{
	ActivePasswordChest.Reset();
	PasswordPawn.Reset();
	EnteredPassword.Reset();
	PasswordEntryMessage.Reset();
	if (!bPasswordInputLocked)
		return;
	bPasswordInputLocked = false;
	SetIgnoreMoveInput(false);
	SetIgnoreLookInput(false);
}

bool ADreamPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (IsEnteringPassword() && Params.Event != IE_Released)
	{
		// 数字只接受新按下，长按不会自动补满四格；删除允许系统重复事件，方便连续删改。
		if (Params.Event == IE_Pressed || (Params.Event == IE_Repeat && Params.Key == EKeys::BackSpace))
			HandlePasswordKey(Params.Key);
		return true;
	}
	// 必须继续向引擎传递释放事件：进入面板前可能正按着 WASD/E，吞掉释放会留下粘滞按键。
	return Super::InputKey(Params);
}

void ADreamPlayerController::HandlePasswordKey(const FKey& Key)
{
	// 提交前也重查生命周期。箱子本帧刚卸载或角色刚换人时，不能依赖上一帧验证结果。
	UpdatePasswordEntry();
	if (!IsEnteringPassword())
		return;
	// 再按 E 也可关闭面板，便于 PIE 中保留编辑器自身的 Esc 停止运行快捷键。
	if (Key == EKeys::Escape || Key == EKeys::E)
	{
		ClosePasswordEntry();
		return;
	}
	if (Key == EKeys::BackSpace)
	{
		if (!EnteredPassword.IsEmpty())
			EnteredPassword.LeftChopInline(1);
		PasswordEntryMessage.Reset();
		return;
	}
	if (Key == EKeys::Enter)
	{
		if (EnteredPassword.Len() != 4)
		{
			PasswordEntryMessage = TEXT("请输入完整的四位密码");
			return;
		}
		ADreamPasswordChest* Chest = ActivePasswordChest.Get();
		ADreamCharacter* ControlledCharacter = Cast<ADreamCharacter>(PasswordPawn.Get());
		if (!Chest->TryUnlock(EnteredPassword, ControlledCharacter))
		{
			PasswordEntryMessage = TEXT("密码错误，请重新输入");
			EnteredPassword.Reset();
		}
		return;
	}
	// 两组数字键显式映射，既不依赖键盘区域设置，也不把 Unicode 数字或键名后缀当密码。
	static const FKey NumberKeys[] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
		EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	static const FKey NumpadKeys[] = { EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree,
		EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };
	for (int32 Digit = 0; Digit < 10; ++Digit)
	{
		if ((Key == NumberKeys[Digit] || Key == NumpadKeys[Digit]) && EnteredPassword.Len() < 4)
		{
			EnteredPassword.AppendChar(TEXT('0') + Digit);
			PasswordEntryMessage.Reset();
			return;
		}
	}
}

void ADreamPlayerController::UpdatePasswordEntry()
{
	if (!IsEnteringPassword())
		return;
	const ADreamPasswordChest* Chest = ActivePasswordChest.Get();
	const ADreamCharacter* ControlledCharacter = Cast<ADreamCharacter>(PasswordPawn.Get());
	if (!IsValid(Chest) || GetPawn() != ControlledCharacter || GetViewTarget() != ControlledCharacter || !Chest->CanInteract(ControlledCharacter) ||
		Chest->GetChestState() != EDreamPasswordChestState::Locked)
	{
		ClosePasswordEntry();
		return;
	}
	// 瞬时自动化世界和无窗口进程可能没有 Viewport；存在真实视口时才检查焦点。
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const FViewport* Viewport = LocalPlayer && LocalPlayer->ViewportClient ? LocalPlayer->ViewportClient->Viewport : nullptr;
	if (Viewport && !Viewport->HasFocus())
		ClosePasswordEntry();
}
