//Easy Multi Save - Copyright (C) 2021 by Michael Hegemann.  

#pragma once

#include "CoreMinimal.h"
#include "EMSData.h"
#include "UObject/ObjectMacros.h"
#include "TimerManager.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "EMSAsyncSaveGame.generated.h"

class UEMSObject;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAsyncSaveOutputPin);

UENUM()
enum class ENextStepType : uint8
{
	SaveLevel,
	FinishSave,
};

UCLASS()
class EASYMULTISAVE_API UEMSAsyncSaveGame : public UBlueprintAsyncActionBase
{
	GENERATED_UCLASS_BODY()

public:

	UPROPERTY(BlueprintAssignable)
	FAsyncSaveOutputPin OnCompleted;

	bool bIsActive;

	ESaveGameMode Mode;

private:

	UObject* WorldContextObject;

	UEMSObject* EMS;

	uint32 Data;

	bool bFinishedStep;

public:

	/**
	* Main function for Saving the Game. Use the Data checkboxes to define what you want to save. 
	*
	* @param Data - Check here what data you want to save. 
	*/
	UFUNCTION(BlueprintCallable, Category = "Easy Multi Save | Actors", meta = (DisplayName = "Save Game Actors", BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UEMSAsyncSaveGame* AsyncSaveActors(UObject* WorldContextObject, UPARAM(meta = (Bitmask, BitmaskEnum = ESaveTypeFlags)) int32 Data);

	virtual void Activate() override;

private:

	void StartSaving();

	void SavePlayer();
	void InternalSavePlayer();

	void SaveLevel();
	void InternalSaveLevel();

	void FinishSaving();
	void CompleteSavingTask();

	void TryMoveToNextStep(ENextStepType Step);

	static ESaveGameMode GetMode(int32 Data);
};


