//Easy Multi Save - Copyright (C) 2021 by Michael Hegemann.  

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "EMSData.h"
#include "EMSPersistentSaveGame.generated.h"

UCLASS()
class EASYMULTISAVE_API UEMSPersistentSaveGame : public USaveGame
{
	GENERATED_BODY()

public:

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Slots")
	FSaveSlotInfo SlotInfo;
};
