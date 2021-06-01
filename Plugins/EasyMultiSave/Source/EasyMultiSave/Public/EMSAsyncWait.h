//Easy Multi Save - Copyright (C) 2021 by Michael Hegemann.  

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EMSData.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "EMSAsyncWait.generated.h"

class UEMSObject;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAsyncWaitOutputPin);

UCLASS()
class EASYMULTISAVE_API UEMSAsyncWait : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
	
public:

	UPROPERTY(BlueprintAssignable)
	FAsyncWaitOutputPin OnCompleted;

	/**
	* Wait until SaveGameActors or LoadGameActors have been completed.
	* For example, this is useful if you want use loaded variables in your BeginPlay event after this node.
	*/
	UFUNCTION(BlueprintCallable, Category = "Easy Multi Save | Actors", meta = (DisplayName = "Wait For Save or Load Completed", BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UEMSAsyncWait* AsyncWaitForOperation(UObject* WorldContextObject);

	virtual void Activate() override;

private:

	UObject* WorldContextObject;
	UEMSObject* EMS;

	void StartWaitTask();
	void CompleteWaitTask();
};
