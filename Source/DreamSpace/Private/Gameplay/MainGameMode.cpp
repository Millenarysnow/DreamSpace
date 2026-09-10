#include "MainGameMode.h"
#include "DreamCharacter.h"
#include "DreamPlayerController.h"
#include "DreamHUD.h"
AMainGameMode::AMainGameMode()
{
	DefaultPawnClass = ADreamCharacter::StaticClass();
	PlayerControllerClass = ADreamPlayerController::StaticClass();
	HUDClass = ADreamHUD::StaticClass();
}
