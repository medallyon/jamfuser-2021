//Easy Multi Save - Copyright (C) 2021 by Michael Hegemann.  

#include "EMSAsyncLoadGame.h"
#include "EMSFunctionLibrary.h"
#include "EMSObject.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Engine.h"


/**
Init
**/

UEMSAsyncLoadGame::UEMSAsyncLoadGame(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

ESaveGameMode UEMSAsyncLoadGame::GetMode(int32 Data)
{
	if (Data & ENUM_TO_FLAG(ELoadTypeFlags::LF_Player))
	{
		if (Data & ENUM_TO_FLAG(ELoadTypeFlags::LF_Level))
		{
			return ESaveGameMode::MODE_All;
		}
		else
		{
			return ESaveGameMode::MODE_Player;
		}
	}

	return ESaveGameMode::MODE_Level;
}

UEMSAsyncLoadGame* UEMSAsyncLoadGame::AsyncLoadActors(UObject* WorldContextObject, int32 Data, bool bFullReload)
{
	if (UEMSObject* EMSObject = UEMSObject::Get(WorldContextObject))
	{
		if (!EMSObject->IsAsyncSaveOrLoadTaskActive(GetMode(Data)))
		{
			UEMSAsyncLoadGame* LoadTask = NewObject<UEMSAsyncLoadGame>(GetTransientPackage());		
			LoadTask->WorldContextObject = WorldContextObject;
			LoadTask->Data = Data;
			LoadTask->Mode = GetMode(Data);
			LoadTask->bFullReload = bFullReload;
			LoadTask->EMS = EMSObject;
			LoadTask->bIsActive = true;

			return LoadTask;
		}
	}

	return nullptr;
}

void UEMSAsyncLoadGame::Activate()
{
	if (EMS)
	{
		EMS->PrepareLoadAndSaveActors(Data, bFullReload);

		EMS->GetTimerManager().SetTimerForNextTick(this, &UEMSAsyncLoadGame::StartLoading);
	}
}

/**
Load Actors
**/

void UEMSAsyncLoadGame::StartLoading()
{
	if (EMS)
	{
		EMS->GetTimerManager().SetTimerForNextTick(this, &UEMSAsyncLoadGame::LoadPlayer);
	}
}

void UEMSAsyncLoadGame::LoadPlayer()
{
	if (EMS)
	{
		if (Data & ENUM_TO_FLAG(ELoadTypeFlags::LF_Player))
		{
			if (!EMS->HasValidPlayer())
			{
				EMS->GetTimerManager().SetTimerForNextTick(this, &UEMSAsyncLoadGame::LoadPlayer);
				return;
			}

			if (EMS->TryLoadPlayerFile())
			{
				EMS->LoadPlayerActors(this);
			}
		}

		EMS->GetTimerManager().SetTimerForNextTick(this, &UEMSAsyncLoadGame::LoadLevel);
	}
}

void UEMSAsyncLoadGame::LoadLevel()
{ 
	if (EMS)
	{
		if (Data & ENUM_TO_FLAG(ELoadTypeFlags::LF_Level))
		{
			if (!EMS->HasValidGameMode())
			{
				EMS->GetTimerManager().SetTimerForNextTick(this, &UEMSAsyncLoadGame::LoadLevel);
				return;
			}

			if (EMS->TryLoadLevelFile())
			{
				EMS->LoadLevelActors(this);
			}
			else
			{
				FinishLoading();
			}
		}
		else
		{
			FinishLoading();
		}
	}
}

/**
Finish
**/

void UEMSAsyncLoadGame::FinishLoading()
{
	if (EMS)
	{
		//Has to be a tick before broadcast.
		bIsActive = false;
		EMS->GetTimerManager().SetTimerForNextTick(this, &UEMSAsyncLoadGame::CompleteLoadingTask);
	}
}

void UEMSAsyncLoadGame::CompleteLoadingTask()
{
	OnCompleted.Broadcast();
	SetReadyToDestroy();
}

/**
Deferred Loading
**/

void UEMSAsyncLoadGame::StartDeferredLoad()
{
	if (EMS)
	{
		SavedActors = EMS->SavedActors;
		DeferredLoadActors();
	}
}

void UEMSAsyncLoadGame::DeferredLoadActors()
{
	if (EMS)
	{
		int Batch = 15; //Rough estimate, we would need benchmark cpu otherwise.
		int LoadNum = 0;

		if (LoadedActorNum < SavedActors.Num())
		{
			while (LoadNum < Batch)
			{
				const int FullBatch = LoadNum + LoadedActorNum;
				if (FullBatch >= SavedActors.Num())
				{
					break;
				}

				const int MinLoadNum = FMath::Min(FullBatch, SavedActors.Num());

				bDeferredLoadSuccess = EMS->SpawnOrUpdateLevelActor(SavedActors[MinLoadNum]);

				LoadNum++;
			}

			LoadedActorNum += Batch;

			EMS->GetTimerManager().SetTimerForNextTick(this, &UEMSAsyncLoadGame::DeferredLoadActors);
		}
		else
		{
			if (bDeferredLoadSuccess)
			{
				EMS->LogFinishLoadingLevel();
			}

			FinishLoading();
		}
	}
}


