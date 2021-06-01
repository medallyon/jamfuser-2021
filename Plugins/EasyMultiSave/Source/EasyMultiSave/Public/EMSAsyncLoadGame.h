//Easy Multi Save - Copyright (C) 2021 by Michael Hegemann.  

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EMSData.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "TimerManager.h"
#include "EMSAsyncLoadGame.generated.h"

class UEMSObject;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAsyncLoadOutputPin);

UCLASS()
class EASYMULTISAVE_API UEMSAsyncLoadGame : public UBlueprintAsyncActionBase
{
	GENERATED_UCLASS_BODY()
	
public:

	UPROPERTY(BlueprintAssignable)
	FAsyncLoadOutputPin OnCompleted;

	bool bIsActive;

	uint32 Data;

	ESaveGameMode Mode;

private:

	UObject* WorldContextObject;

	UEMSObject* EMS;

	bool bFullReload;

	bool bDeferredLoadSuccess;
	int LoadedActorNum;
	TArray<FActorSaveData> SavedActors;

public:
	
	/**
	* Main function for Loading the Game. Use the Data checkboxes to define what you want to load.
	*
	* @param Data - Check here what data you want to load.
	* @param bFullReload - If false, load only Actors that have not been loaded. Set to true if you want to reload all saved Actor Data.
	*/
	UFUNCTION(BlueprintCallable, Category = "Easy Multi Save | Actors", meta = (DisplayName = "Load Game Actors", BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UEMSAsyncLoadGame* AsyncLoadActors(UObject* WorldContextObject, UPARAM(meta = (Bitmask, BitmaskEnum = ELoadTypeFlags)) int32 Data, bool bFullReload);

	virtual void Activate() override;

	void FinishLoading();

	void StartDeferredLoad();

private:

	void StartLoading();
	void LoadPlayer();
	void LoadLevel();
	void CompleteLoadingTask();

	void DeferredLoadActors();

	static ESaveGameMode GetMode(int32 Data);
};
