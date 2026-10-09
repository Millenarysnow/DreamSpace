#include "DreamDroppedKey.h"

#include "DreamKeyPickupComponent.h"

ADreamDroppedKey::ADreamDroppedKey()
{
	KeyPickup = CreateDefaultSubobject<UDreamKeyPickupComponent>(TEXT("KeyPickup"));
}

bool ADreamDroppedKey::ActivateDrop()
{
	if (!Super::ActivateDrop())
		return false;
	KeyPickup->EnablePickup();
	return true;
}
