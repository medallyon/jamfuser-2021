//Easy Multi Save - Copyright (C) 2021 by Michael Hegemann.  

#include "EMSObject.h"
#include "EMSFunctionLibrary.h"
#include "HAL/PlatformFilemanager.h"
#include "EMSActorSaveInterface.h"
#include "Misc/FileHelper.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Serialization/ArchiveSaveCompressedProxy.h"
#include "Serialization/ArchiveLoadCompressedProxy.h"
#include "TimerManager.h"
#include "EMSPluginSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"
#include "Runtime/Launch/Resources/Version.h"
#include "ImageUtils.h"

/**
Initalization
**/

UWorld* UEMSObject::GetWorld() const
{
	return World;
}

UEMSObject* UEMSObject::Get(UObject* WorldContextObject)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World)
	{
		UE_LOG(LogEasyMultiSave, Error, TEXT("Easy Multi Save has no valid World"));
		return nullptr;
	}

	bool bIsDedicatedServer = UKismetSystemLibrary::IsDedicatedServer(WorldContextObject);
	AActor* OuterActor = nullptr;

	//Use GameMode as Owner for dedicated servers, otherwise use the local PlayerController.
	if (bIsDedicatedServer)
	{
		AGameModeBase* GameMode = UGameplayStatics::GetGameMode(WorldContextObject);

		if (!IsValid(GameMode))
		{
			return nullptr;
		}

		OuterActor = GameMode;
	}
	else
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContextObject, PlayerIndex);

		if (!PC)
		{
			return nullptr;
		}

		if (PC && !PC->IsLocalPlayerController())
		{
			return nullptr;
		}

		OuterActor = PC;
	}

	if (!IsValid(OuterActor))
	{
		UE_LOG(LogEasyMultiSave, Error, TEXT("Easy Multi Save has no valid Outer"));
		return nullptr;
	}

	//Refer to existing
	UObject* Object = StaticCast<UObject*>(FindObjectWithOuter(OuterActor, UEMSObject::StaticClass()));
	if (Object)
	{
		UEMSObject* EMSObject = Cast<UEMSObject>(Object);
		if (EMSObject)
		{
			//When seamless traveling, we don't want to hit the old world in memory.
			EMSObject->World = World;
			return EMSObject;
		}
	}
	
	//Create the object with the desired outer, so we can find it easily. 
	UEMSObject* EMSObject = NewObject<UEMSObject>(OuterActor);
	if (EMSObject)
	{
		//Prevent GC while the outer is valid.
		OuterActor->OnEndPlay.AddDynamic(EMSObject, &UEMSObject::OuterActorEndPlay);

		EMSObject->AddToRoot();		
		EMSObject->World = World;

		UE_LOG(LogEasyMultiSave, Log, TEXT("Easy Multi Save Initialized"));
		UE_LOG(LogEasyMultiSave, Log, TEXT("Current Save Game Slot is: %s"), *EMSObject->GetCurrentSaveGameName());

		return EMSObject;
	}

	return nullptr;
}

void UEMSObject::OuterActorEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason)
{
	if (!IsPendingKillOrUnreachable() && IsRooted())
	{
		RemoveFromRoot();
	}
}

/**
Local Profile
Fully seperate of the other save functions.
**/

bool UEMSObject::SaveLocalProfile()
{
	UEMSProfileSaveGame* ProfileSaveGame = GetLocalProfileSaveGame();
	if (ProfileSaveGame)
	{
		if (SaveObject(*ProfileSaveFile(), ProfileSaveGame))
		{
			UE_LOG(LogEasyMultiSave, Log, TEXT("Local Profile saved"));
			return true;
		}
	}

	return false;
}

UEMSProfileSaveGame* UEMSObject::GetLocalProfileSaveGame()
{
	return GetDesiredSaveObject<UEMSProfileSaveGame>(
		ProfileSaveFile(),
		UEMSPluginSettings::Get()->ProfileSaveGameClass,
		CachedProfileSave);
}

/**
Persistent Save Game
**/

bool UEMSObject::SavePersistentObject()
{
	SaveSlotInfoObject();

	const FString SaveGameName = GetCurrentSaveGameName();
	if (VerifyOrCreateDirectory(SaveGameName))
	{
		UEMSPersistentSaveGame* SaveGame = GetPersistentSave();
		if (SaveObject(*PersistentSaveFile(), SaveGame))
		{
			UE_LOG(LogEasyMultiSave, Log, TEXT("Persistent Save Game saved"));
			return true;
		}
	}	

	return false;
}

UEMSPersistentSaveGame* UEMSObject::GetPersistentSave()
{
	return GetDesiredSaveObject<UEMSPersistentSaveGame>(
		PersistentSaveFile(),
		UEMSPluginSettings::Get()->PersistentSaveGameClass,
		CachedPersistentSave);
}

/**
Save Slots
**/

FString UEMSObject::GetCurrentSaveGameName()
{
	FString DefaultName = UEMSPluginSettings::Get()->DefaultSaveGameName;

	if (CurrentSaveGameName.IsEmpty())
	{
		return DefaultName;
	}

	return CurrentSaveGameName;
}

UEMSInfoSaveGame* UEMSObject::GetSlotInfoObject(FString SaveGameName)
{
	//We need to clear the cached one for Get Named Slot Info
	if (!SaveGameName.IsEmpty())
	{
		CachedSlotInfoSave = nullptr;
	}

	return GetDesiredSaveObject<UEMSInfoSaveGame>(
		SlotInfoSaveFile(SaveGameName),
		UEMSPluginSettings::Get()->SlotInfoSaveGameClass,
		CachedSlotInfoSave);
}

void UEMSObject::SaveSlotInfoObject()
{
	const FString SaveGameName = GetCurrentSaveGameName();

	if (VerifyOrCreateDirectory(SaveGameName))
	{
		UEMSInfoSaveGame* SaveGame = GetSlotInfoObject();
		if (SaveGame)
		{
			//GetSortedSaveSlots() only uses the file data, this uses actual saved data.
			SaveGame->SlotInfo.Name = SaveGameName;
			SaveGame->SlotInfo.TimeStamp = FDateTime::Now();
			SaveGame->SlotInfo.Level = GetLevelName();

			TArray<FString> PlayerNames;
			if (AGameStateBase* GameState = GetWorld()->GetGameState())
			{
				TArray<APlayerState*> Players = GameState->PlayerArray;
				if (Players.Num() > 0)
				{
					for (APlayerState* PlayerState : Players)
					{
						PlayerNames.Add(PlayerState->GetPlayerName());
					}

					SaveGame->SlotInfo.Players = PlayerNames;
				}
			}

			SaveObject(*SlotInfoSaveFile(), SaveGame);
		}
	}
}

void UEMSObject::SetCurrentSaveGameName(const FString & SaveGameName)
{
	if (CurrentSaveGameName != SaveGameName)
	{
		CachedSlotInfoSave = nullptr; //New slot name, so we have to clear the cached one.
		CachedPersistentSave = nullptr; //Also clear Persistent Save 

		const FString OldSaveName = CurrentSaveGameName;

		CurrentSaveGameName = SaveGameName;
		SaveConfig();

		UE_LOG(LogEasyMultiSave, Log, TEXT("New Current Save Game Slot is: %s"), *SaveGameName);

		//Copy persistent file if desired. 
		if (UEMSPluginSettings::Get()->bCopyPersistentSave)
		{
			if (VerifyOrCreateDirectory(SaveGameName))
			{
				const FString SrcPersistentFile = PersistentSaveFile(OldSaveName);
				if (IFileManager::Get().Copy(*PersistentSaveFile(), *SrcPersistentFile, true, false))
				{
					UE_LOG(LogEasyMultiSave, Log, TEXT("Copy of Persistent Save Object to New Slot was successful."));
				}
			}
		}
	}
}

TArray<FString> UEMSObject::GetSortedSaveSlots()
{
	TArray<FString> SaveSlotNames;
	TArray<FSaveSlotInfo> SaveSlots;

	TArray<FString> SaveGameNames;
	IFileManager::Get().FindFiles(SaveGameNames, *FPaths::Combine(BaseSaveDir(), TEXT("*")), false, true);

	for (FString SlotName : SaveGameNames)
	{
		FSaveSlotInfo SlotInfo;
		SlotInfo.Name = SlotName;

		//Use Timestamp of actual file ONLY for sorting, it's faster, but it gives wrong system time.
		SlotInfo.TimeStamp = IFileManager::Get().GetTimeStamp(*SlotInfoSaveFile(SlotName));

		SaveSlots.Add(SlotInfo);
	}

	SaveSlots.Sort([](const FSaveSlotInfo& A, const FSaveSlotInfo& B)
	{
		return A.TimeStamp > B.TimeStamp;
	});

	for (FSaveSlotInfo SlotInfo : SaveSlots)
	{
		SaveSlotNames.Add(SlotInfo.Name);
	}
	
	return SaveSlotNames;
}

bool UEMSObject::DoesSaveGameExist(const FString& SaveGameName)
{
	const FString SaveFile = FPaths::Combine(BaseSaveDir(), SaveGameName);

	if (IFileManager::Get().DirectoryExists(*SaveFile))
	{
		return true;
	}

	return false;
}

/***
Save Users
**/

void UEMSObject::SetCurrentSaveUserName(const FString& UserName)
{
	if (CurrentSaveUserName != UserName)
	{
		CachedSlotInfoSave = nullptr;
		CachedPersistentSave = nullptr;

		CurrentSaveUserName = UserName;
		SaveConfig();

		UE_LOG(LogEasyMultiSave, Log, TEXT("New Current Save User Name is: %s"), *UserName);
	}
}

void UEMSObject::DeleteAllSaveDataForUser(const FString& UserName)
{
	if (CurrentSaveUserName != UserName)
	{
		const FString UserSaveFile = FPaths::Combine(UEMSObject::SaveUserDir(), UserName);
		bool bSuccess = false;

		//Try removing folder	
		bSuccess = IFileManager::Get().DeleteDirectory(*UserSaveFile, true, true);
		if (bSuccess)
		{
			UE_LOG(LogEasyMultiSave, Log, TEXT("Save Game User Data removed for: %s"), *UserName);
		}
	}
}

TArray<FString> UEMSObject::GetAllSaveUsers()
{
	TArray<FString> SaveUserNames;
	IFileManager::Get().FindFiles(SaveUserNames, *FPaths::Combine(UEMSObject::SaveUserDir(), TEXT("*")), false, true);

	return SaveUserNames;
}

/**
File System
**/

bool UEMSObject::VerifyOrCreateDirectory(const FString& NewDir)
{
	const FString SaveFile = FPaths::Combine(BaseSaveDir(), NewDir);

	if (IFileManager::Get().DirectoryExists(*SaveFile))
	{
		return true;
	}

	return IFileManager::Get().MakeDirectory(*SaveFile, true);
}

void UEMSObject::DeleteAllSaveDataForSlot(const FString& SaveGameName)
{
	const FString SaveFile = FPaths::Combine(UEMSObject::BaseSaveDir(), SaveGameName);
	bool bSuccess = false;

	//Try removing folder	
	bSuccess = IFileManager::Get().DeleteDirectory(*SaveFile, true, true);
	if (bSuccess)
	{
		UE_LOG(LogEasyMultiSave, Log, TEXT("Save Game Data removed for: %s"), *SaveGameName);
	}
}

USaveGame* UEMSObject::CreateSaveObject(TSubclassOf<USaveGame> SaveGameClass)
{
	if (*SaveGameClass && (*SaveGameClass != USaveGame::StaticClass()))
	{
		USaveGame* SaveGame = NewObject<USaveGame>(GetTransientPackage(), SaveGameClass);
		return SaveGame;
	}

	return nullptr;
}

bool UEMSObject::SaveObject(const FString& FullSavePath, USaveGame* SaveGameObject)
{
	bool bSuccess = false;

	if (SaveGameObject)
	{
		TArray<uint8> Data;

		FMemoryWriter MemoryWriter(Data, true);
		FObjectAndNameAsStringProxyArchive Ar(MemoryWriter, false);
		SaveGameObject->Serialize(Ar);

		FBufferArchive Archive;
		Archive << Data;

		bSuccess = SaveBinaryArchive(Archive, *FullSavePath);

		Archive.FlushCache();
		Archive.Close();

		MemoryWriter.FlushCache();
		MemoryWriter.Close();
	}

	return bSuccess;
}

USaveGame* UEMSObject::LoadObject(const FString& FullSavePath, TSubclassOf<USaveGame> SaveGameClass)
{
	USaveGame* SaveGameObject = CreateSaveObject(SaveGameClass);
	if (SaveGameObject)
	{
		LoadBinaryArchive(EDataLoadType::DATA_Object, FullSavePath, SaveGameObject);
	}

	return SaveGameObject;
}

/**
Archive Functions
**/

bool UEMSObject::SaveBinaryArchive(FBufferArchive& BinaryData, const FString& FullSavePath)
{
	//Compress and save

	TArray<uint8> CompressedData;

	FArchiveSaveCompressedProxy Compressor = FArchiveSaveCompressedProxy(CompressedData, NAME_Zlib);

	if (Compressor.GetError())
	{
		UE_LOG(LogEasyMultiSave, Error, TEXT("Cannot save, compressor error: %s"), *FullSavePath);
		return false;
	}

	Compressor << BinaryData;
	Compressor.Flush();

	bool bSuccess = FFileHelper::SaveArrayToFile(CompressedData, *FullSavePath);

	Compressor.FlushCache();
	CompressedData.Empty();
	Compressor.Close();

	BinaryData.FlushCache();
	BinaryData.Empty();
	BinaryData.Close();

	return bSuccess;
}

bool UEMSObject::LoadBinaryArchive(EDataLoadType LoadType, const FString& FullSavePath, UObject* Object)
{
	if (IFileManager::Get().FileSize(*FullSavePath) <= 0)
	{
		return false;
	}

	TArray<uint8> BinaryData;
	if (!FFileHelper::LoadFileToArray(BinaryData, *FullSavePath))
	{
		UE_LOG(LogEasyMultiSave, Warning, TEXT("%s could not be loaded"), *FullSavePath);
		return false;
	}

	if (BinaryData.Num() <= 0)
	{
		UE_LOG(LogEasyMultiSave, Warning, TEXT("No binary data found for %s"), *FullSavePath);
		return false;
	}

	//Decompress and load 

	FArchiveLoadCompressedProxy Decompressor = FArchiveLoadCompressedProxy(BinaryData, NAME_Zlib);

	if (Decompressor.GetError())
	{
		UE_LOG(LogEasyMultiSave, Error, TEXT("Cannot load, file might not be compressed: %s"), *FullSavePath);
		return false;
	}

	FBufferArchive DecompressedBinary;
	Decompressor << DecompressedBinary;

	FMemoryReader FromBinary = FMemoryReader(DecompressedBinary, true);
	FromBinary.Seek(0);

	//Unpack archive and do stuff
	bool bSuccess = UnpackBinaryArchive(FromBinary, LoadType, Object);

	Decompressor.FlushCache();
	Decompressor.Close();

	DecompressedBinary.Empty();
	DecompressedBinary.Close();

	BinaryData.Empty();

	FromBinary.FlushCache();
	FromBinary.Close();

	return bSuccess;
}

bool UEMSObject::UnpackBinaryArchive(FMemoryReader FromBinary, EDataLoadType LoadType, UObject* Object)
{
	if (LoadType == EDataLoadType::DATA_Level)
	{
		FLevelArchive LevelArchive;
		FromBinary << LevelArchive;

		bool bLevelLoadSuccess = false;

		//SavedActors are manually added
		SavedActors.Empty();
		for (FActorSaveData TempSavedActor : LevelArchive.SavedActors)
		{
			if (EActorType(TempSavedActor.Type) ==  EActorType::AT_Persistent)
			{
				SavedActors.Add(TempSavedActor);
				bLevelLoadSuccess = true;
			}
			else
			{
				if (LevelArchive.Level == GetLevelName())
				{
					SavedActors.Add(TempSavedActor);
					bLevelLoadSuccess = true;
				}
			}
		}

		if (LevelArchive.Level == GetLevelName())
		{
			SavedScripts = LevelArchive.SavedScripts;

			bLevelLoadSuccess = true;
		}

		if (LevelArchive.Level == GetLevelName() || UEMSPluginSettings::Get()->bPersistentGameMode)
		{
			SavedGameMode = LevelArchive.SavedGameMode;
			SavedGameState = LevelArchive.SavedGameState;

			bLevelLoadSuccess = true;
		}

		return bLevelLoadSuccess;
	}
	else if (LoadType == EDataLoadType::DATA_Player)
	{
		FPlayerArchive PlayerArchive;
		FromBinary << PlayerArchive;

		if (PlayerArchive.Level == GetLevelName() || UEMSPluginSettings::Get()->bPersistentPlayer)
		{
			SavedController = PlayerArchive.SavedController;
			SavedPawn = PlayerArchive.SavedPawn;
			SavedPlayerState = PlayerArchive.SavedPlayerState;

			return true;
		}
	}
	else if (LoadType == EDataLoadType::DATA_Object)
	{
		if (Object)
		{
			FBufferArchive ObjectArchive;
			FromBinary << ObjectArchive;

			FMemoryReader MemoryReader(ObjectArchive, true);
			FObjectAndNameAsStringProxyArchive Ar(MemoryReader, true);
			Object->Serialize(Ar);

			ObjectArchive.FlushCache();
			ObjectArchive.Close();

			MemoryReader.FlushCache();
			MemoryReader.Close();

			return true;
		}
	}

	return false;
}

bool UEMSObject::TryLoadPlayerFile()
{
	return LoadBinaryArchive(EDataLoadType::DATA_Player, PlayerSaveFile());
}

bool UEMSObject::TryLoadLevelFile()
{
	return LoadBinaryArchive(EDataLoadType::DATA_Level, ActorSaveFile());
}

/**
Saving Game Actors General Functions
**/

void UEMSObject::PrepareLoadAndSaveActors(uint32 Flags, bool bFullReload)
{
	TArray<AActor*> Actors;
	for (FActorIterator It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && IsValidActor(Actor))
		{
		
			if (bFullReload)
			{		
				//Thanks Sid Spacewalker
				bool bIsPlayer = false;
				if (GetPlayerController())
				{
					bIsPlayer = (Actor == GetPlayerController()) || (Actor == GetPlayerPawn()) || (Actor == GetPlayerController()->PlayerState);
				}

				if (Flags & ENUM_TO_FLAG(ELoadTypeFlags::LF_Player))
				{
					if (bIsPlayer)
					{
						Actor->Tags.Remove(HasLoadedTag);
					}
				}

				if (Flags & ENUM_TO_FLAG(ELoadTypeFlags::LF_Level))
				{
					if (!bIsPlayer)
					{
						Actor->Tags.Remove(HasLoadedTag);
					}
				}
			}

			//For all, including player, must be done a tick before actual save/load
			SerializeActorStructProperties(Actor);

			EActorType Type = GetActorType(Actor);
			if (Type == EActorType::AT_Runtime || Type == EActorType::AT_Placed || Type == EActorType::AT_LevelScript || Type == EActorType::AT_Persistent)
			{
				Actors.Add(Actor);
			}
		}
	}

	ActorList.Empty();
	ActorList = Actors;
}

/**
Saving and Loading Level Actors
**/

void UEMSObject::SaveLevelActors()
{
	TArray<FActorSaveData> InActors;
	TArray<FLevelScriptSaveData> InScripts;
	FGameObjectSaveData InGameMode;
	FGameObjectSaveData InGameState;

	for (AActor* Actor : ActorList)
	{
		if (Actor && IsValidForSaving(Actor))
		{
			EActorType Type = GetActorType(Actor);

			//Add Level Actor and Component Data
			if (Type == EActorType::AT_Runtime || Type == EActorType::AT_Placed || Type == EActorType::AT_Persistent)
			{
				FActorSaveData ActorArray;

				if (Type == EActorType::AT_Runtime || Type == EActorType::AT_Persistent)
				{
					ActorArray.Class = BytesFromString(Actor->GetClass()->GetPathName());
				}
			
				ActorArray.Type = uint8(Type);
				
				//No transform for persistent Actors or if skipped
				if (Type == EActorType::AT_Persistent || Actor->ActorHasTag(SkipTransformTag))
				{		
					ActorArray.Transform = FTransform::Identity;	
				}
				else
				{
					ActorArray.Transform = Actor->GetActorTransform();
				}
				
				ActorArray.Name = BytesFromString(Actor->GetName());

				SaveActorToBinary(Actor, ActorArray.SaveData);
				InActors.Add(ActorArray);
			}
			//Add Level Script Data
			else if (Type == EActorType::AT_LevelScript)
			{
				FLevelScriptSaveData ScriptArray;
				ScriptArray.Name = LevelScriptSaveName(Actor);

				SaveActorToBinary(Actor, ScriptArray.SaveData);
				InScripts.Add(ScriptArray);
			}
		}
	}
	
	//Game Mode Actor
	AGameModeBase* GameMode = GetWorld()->GetAuthGameMode();
	if (GameMode && IsValidForSaving(GameMode))
	{
		SaveActorToBinary(GameMode, InGameMode);
	}

	//Game State Actor
	AGameStateBase* GameState = GetWorld()->GetGameState();
	if (GameState && IsValidForSaving(GameState))
	{
		SaveActorToBinary(GameState, InGameState);
	}

	FLevelArchive LevelArchive;
	LevelArchive.SavedActors = InActors;
	LevelArchive.SavedScripts = InScripts;
	LevelArchive.SavedGameMode = InGameMode;
	LevelArchive.SavedGameState = InGameState;
	LevelArchive.Level = GetLevelName();

	FBufferArchive LevelData;
	LevelData << LevelArchive;

	if (SaveBinaryArchive(LevelData, ActorSaveFile()))
	{
		UE_LOG(LogEasyMultiSave, Log, TEXT("Level and Game Actors have been saved"));
	}
	else
	{
		UE_LOG(LogEasyMultiSave, Error, TEXT("Failed to save Level Actors"));
	}
}

void UEMSObject::LoadLevelActors(UEMSAsyncLoadGame* LoadTask)
{
	//Level Scripts
	if (SavedScripts.Num() > 0)
	{
		for (AActor* Actor : ActorList)
		{
			if (Actor && IsValidForLoading(Actor) && GetActorType(Actor) == EActorType::AT_LevelScript)
			{
				for (FLevelScriptSaveData ScriptArray : SavedScripts)
				{
					//Compare by level name, since the engine creates multiple script actors.
					const FName ScriptName = LevelScriptSaveName(Actor);

					if (ScriptArray.Name == ScriptName)
					{
						UE_LOG(LogEasyMultiSave, Log, TEXT("%s Blueprint Loaded"), *ScriptName.ToString());

						LoadActorFromBinary(Actor, ScriptArray.SaveData);
					}
				}
			}
		}
	}

	//Game Mode Actor
	if (SavedGameMode.Data.Num() > 0)
	{
		AGameModeBase* GameMode = GetWorld()->GetAuthGameMode();
		if (GameMode && IsValidForLoading(GameMode))
		{
			LoadActorFromBinary(GameMode, SavedGameMode);

			UE_LOG(LogEasyMultiSave, Log, TEXT("Game Mode loaded"));
		}
	}

	//Game State Actor
	if (SavedGameState.Data.Num() > 0)
	{
		AGameStateBase* GameState = GetWorld()->GetGameState();
		if (GameState && IsValidForLoading(GameState))
		{
			LoadActorFromBinary(GameState, SavedGameState);

			UE_LOG(LogEasyMultiSave, Log, TEXT("Game State loaded"));
		}
	}

	//Level Actors
	StartLoadLevelActors(LoadTask);
}

void UEMSObject::StartLoadLevelActors(UEMSAsyncLoadGame* LoadTask)
{
	if (!LoadTask)
	{
		return;
	}

	if (SavedActors.Num() <= 0)
	{
		LoadTask->FinishLoading();
		return;
	}

	//If authority, we use distance based loading
	if (GetWorld()->IsServer())
	{
		APlayerController* PC = GetPlayerController();
		if (PC && PC->PlayerCameraManager)
		{
			FVector CameraLoc = PC->PlayerCameraManager->GetCameraLocation();
			SavedActors.Sort([CameraLoc](const FActorSaveData& A, const FActorSaveData& B)
			{
				float DistA = FVector::Dist(A.Transform.GetLocation(), CameraLoc);
				float DistB = FVector::Dist(B.Transform.GetLocation(), CameraLoc);

				return DistA < DistB;
			});
		}
	}

	if (UEMSPluginSettings::Get()->LoadMethod == ELoadMethod::LM_Thread)
	{
		if (FPlatformProcess::SupportsMultithreading())
		{
			AsyncTask(ENamedThreads::AnyNormalThreadNormalTask, [this, LoadTask]()
			{
				LoadAllLevelActors(LoadTask);
			});
		}
		else
		{
			LoadTask->StartDeferredLoad();
		}
	}
	else if (UEMSPluginSettings::Get()->LoadMethod == ELoadMethod::LM_Deferred)
	{
		LoadTask->StartDeferredLoad();
	}
	else
	{
		LoadAllLevelActors(LoadTask);
	}
	
}

void UEMSObject::LoadAllLevelActors(UEMSAsyncLoadGame* LoadTask)
{
	bool bSuccess = false;

	for (FActorSaveData ActorArray : SavedActors)
	{
		bSuccess = SpawnOrUpdateLevelActor(ActorArray);
	}

	if (bSuccess)
	{
		LogFinishLoadingLevel();
	}

	if (!IsInGameThread())
	{
		AsyncTask(ENamedThreads::GameThread, [LoadTask]()
		{
			LoadTask->FinishLoading();
		});
	}
	else
	{
		LoadTask->FinishLoading();
	}
	
}

bool UEMSObject::SpawnOrUpdateLevelActor(const FActorSaveData& ActorArray)
{
	EActorType Type = EActorType(ActorArray.Type);

	bool bRightType = Type == EActorType::AT_Placed || Type == EActorType::AT_Runtime || Type == EActorType::AT_Persistent;
	if (!bRightType)
	{
		return false;
	}

	EUpdateActorResult UpdateResult = UpdateLevelActor(ActorArray);
	if (UpdateResult == EUpdateActorResult::RES_ShouldSpawnNewActor)
	{
		SpawnLevelActor(ActorArray);
	}

	return UpdateResult != EUpdateActorResult::RES_Skip;
}

EUpdateActorResult UEMSObject::UpdateLevelActor(const FActorSaveData& ActorArray)
{
	for (AActor* Actor : ActorList)
	{
		if (Actor && IsValidActor(Actor))
		{
			//Update existing actors
			if (ActorArray.Name == BytesFromString(Actor->GetName()))
			{
				//Skips respawn
				if (Actor->ActorHasTag(HasLoadedTag))
				{
					return EUpdateActorResult::RES_Skip;
				}

				if (!IsInGameThread())
				{
					AsyncTask(ENamedThreads::GameThread, [this, Actor, ActorArray]()
					{
						ProcessLevelActor(Actor, ActorArray);
					});
				}
				else
				{
					ProcessLevelActor(Actor, ActorArray);
				}

				return EUpdateActorResult::RES_Success;
			}	
		}
	}

	return EUpdateActorResult::RES_ShouldSpawnNewActor;
}

bool UEMSObject::CheckForExistingActor(const FActorSaveData& ActorArray)
{
	if (!UEMSPluginSettings::Get()->bAdvancedSpawnCheck)
	{
		return false;
	}

	const UWorld* ThisWorld = GetWorld();
	if (ThisWorld && ThisWorld->PersistentLevel)
	{
		const FName LoadedActorName(*StringFromBytes(ActorArray.Name));
		AActor* NewLevelActor = Cast<AActor>(StaticFindObjectFast(nullptr, GetWorld()->PersistentLevel, LoadedActorName));
		if (NewLevelActor)
		{
			ProcessLevelActor(NewLevelActor, ActorArray);
			return true;
		}
	}

	return false;
}

void UEMSObject::SpawnLevelActor(const FActorSaveData & ActorArray)
{
	const FString Class = StringFromBytes(ActorArray.Class);
	UClass* SpawnClass = FindObject<UClass>(ANY_PACKAGE, *Class);

	//Thanks Wertix
	if (!SpawnClass)
	{
		SpawnClass = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, *Class, nullptr, LOAD_None, nullptr));
	}

	if (SpawnClass && SpawnClass->ImplementsInterface(UEMSActorSaveInterface::StaticClass()))
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.Name = FName(*StringFromBytes(ActorArray.Name));
		SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;  //don't crash

		if (!IsInGameThread())
		{
			AsyncTask(ENamedThreads::GameThread, [this, ActorArray, SpawnClass, SpawnParams]()
			{
				if (!CheckForExistingActor(ActorArray))
				{
					AActor* NewActor = GetWorld()->SpawnActor(SpawnClass, &ActorArray.Transform, SpawnParams);
					if (NewActor)
					{
						ProcessLevelActor(NewActor, ActorArray);
					}
				}
			});
		}
		else
		{
			if (!CheckForExistingActor(ActorArray))
			{
				AActor* NewActor = GetWorld()->SpawnActor(SpawnClass, &ActorArray.Transform, SpawnParams);
				if (NewActor)
				{
					ProcessLevelActor(NewActor, ActorArray);
				}
			}
		}
	}
}

void UEMSObject::ProcessLevelActor(AActor* Actor, const FActorSaveData& ActorArray)
{
	//Only process matching type
	if (EActorType(ActorArray.Type) == GetActorType(Actor))
	{
		if (IsMovable(Actor->GetRootComponent()) && ActorArray.Transform.IsValid())
		{
			Actor->SetActorTransform(ActorArray.Transform, false, nullptr, ETeleportType::TeleportPhysics);
		}

		LoadActorFromBinary(Actor, ActorArray.SaveData);
	}
}

void UEMSObject::LogFinishLoadingLevel()
{
	UE_LOG(LogEasyMultiSave, Log, TEXT("Level Actors loaded"));
}

/**
Saving and Loading Player
**/

void UEMSObject::SavePlayerActors()
{
	bool bPlayerSaveSuccess = false;

	//Controller
	FControllerSaveData InController;
	APlayerController* Controller = GetPlayerController();
	if (Controller && IsValidForSaving(Controller))
	{
		if (!Controller->ActorHasTag(SkipTransformTag))
		{	
			InController.Rotation = Controller->GetControlRotation();
		}

		SaveActorToBinary(Controller, InController.SaveData);

		bPlayerSaveSuccess = true;
	}

	//Pawn
	FPawnSaveData InPawn;
	APawn* Pawn = GetPlayerPawn();
	if (Pawn && IsValidForSaving(Pawn))
	{
		if (!Pawn->ActorHasTag(SkipTransformTag))
		{
			InPawn.Position = Pawn->GetActorLocation();
			InPawn.Rotation = Pawn->GetActorRotation();
		}

		SaveActorToBinary(Pawn, InPawn.SaveData);

		bPlayerSaveSuccess = true;
	}

	//Player State
	FGameObjectSaveData InPlayerState;
	APlayerState* PlayerState = GetPlayerController()->PlayerState;
	if (PlayerState && IsValidForSaving(PlayerState))
	{
		SaveActorToBinary(PlayerState, InPlayerState);

		bPlayerSaveSuccess = true;
	}

	FPlayerArchive PlayerArchive;
	PlayerArchive.SavedController = InController;
	PlayerArchive.SavedPawn = InPawn;
	PlayerArchive.SavedPlayerState = InPlayerState;
	PlayerArchive.Level = GetLevelName();

	FBufferArchive PlayerData;
	PlayerData << PlayerArchive;

	if (SaveBinaryArchive(PlayerData, PlayerSaveFile()))
	{
		if (bPlayerSaveSuccess)
		{
			UE_LOG(LogEasyMultiSave, Log, TEXT("Player Actors have been saved"));
		}
	}
	else
	{
		UE_LOG(LogEasyMultiSave, Error, TEXT("Failed to save Player"));
	}
}

void UEMSObject::LoadPlayerActors(UEMSAsyncLoadGame* LoadTask)
{
	//Controller
	APlayerController* Controller = GetPlayerController();
	if (Controller && IsValidForLoading(Controller))
	{
		const FControllerSaveData ControllerData = SavedController;

		if (!UEMSPluginSettings::Get()->bPersistentPlayer && !ControllerData.Rotation.IsNearlyZero())
		{
			Controller->SetControlRotation(ControllerData.Rotation);
		}

		LoadActorFromBinary(Controller, ControllerData.SaveData);

		UE_LOG(LogEasyMultiSave, Log, TEXT("Player Controller loaded"));
	}

	//Pawn
	APawn* Pawn = GetPlayerPawn();
	if (Pawn && IsValidForLoading(Pawn))
	{
		const FPawnSaveData PawnData = SavedPawn;

		if (!UEMSPluginSettings::Get()->bPersistentPlayer && !PawnData.Position.IsNearlyZero())
		{
			Pawn->SetActorLocation(PawnData.Position, false, nullptr, ETeleportType::TeleportPhysics);
			Pawn->SetActorRotation(PawnData.Rotation, ETeleportType::TeleportPhysics);
		}

		LoadActorFromBinary(Pawn, PawnData.SaveData);

		UE_LOG(LogEasyMultiSave, Log, TEXT("Player Pawn loaded"));
	}

	//Player State
	if (SavedPlayerState.Data.Num() > 0)
	{
		APlayerState* PlayerState = GetPlayerController()->PlayerState;
		if (PlayerState && IsValidForLoading(PlayerState))
		{
			LoadActorFromBinary(PlayerState, SavedPlayerState);

			UE_LOG(LogEasyMultiSave, Log, TEXT("Player State loaded"))
		}
	}
}

/**
Loading and Saving Components
**/

void UEMSObject::SaveActorComponents(AActor* Actor, TArray<FComponentSaveData>& OutComponents)
{
	TArray<UActorComponent*> SourceComps;
	IEMSActorSaveInterface::Execute_ComponentsToSave(Actor, SourceComps);

	if (SourceComps.Num() <= 0)
	{
		return;
	}

	for (UActorComponent* Component : SourceComps)
	{
		if (Component && Component->IsRegistered())
		{
			FComponentSaveData ComponentArray;
			ComponentArray.Name = BytesFromString(Component->GetName());

			USceneComponent* SceneComp = Cast<USceneComponent>(Component);
			if (SceneComp)
			{
				ComponentArray.RelativeTransform = SceneComp->GetRelativeTransform();
			}

			UChildActorComponent* ChildActorComp = Cast<UChildActorComponent>(Component);
			if (ChildActorComp)
			{
				AActor* ChildActor = ChildActorComp->GetChildActor();
				if (ChildActor)
				{
					if (!HasSaveInterface(ChildActor))
					{
						SerializeToBinary(ChildActor, ComponentArray.Data);
					}
					else
					{
						UE_LOG(LogEasyMultiSave, Warning, TEXT("Child Actor Component has Save Interface, skipping: %s"), *Component->GetName());
					}
				}
			}
			else
			{
				SerializeToBinary(Component, ComponentArray.Data);
			}

			//UE_LOG(LogEasyMultiSave, Log, TEXT("Component Saved: %s"), *Component->GetName());

			OutComponents.Add(ComponentArray);
		}
	}
}

void UEMSObject::LoadActorComponents(AActor* Actor, const TArray<FComponentSaveData>& InComponents)
{
	TArray<UActorComponent*> SourceComps;
	IEMSActorSaveInterface::Execute_ComponentsToSave(Actor, SourceComps);

	if (SourceComps.Num() <= 0)
	{
		return;
	}

	for (UActorComponent* Component : SourceComps)
	{
		if (Component && Component->IsRegistered())
		{
			for (FComponentSaveData ComponentArray : InComponents)
			{
				if (ComponentArray.Name == BytesFromString(Component->GetName()))
				{
					USceneComponent* SceneComp = Cast<USceneComponent>(Component);
					if (SceneComp)
					{
						if (IsMovable(SceneComp))
						{
							SceneComp->SetRelativeTransform(ComponentArray.RelativeTransform, false, nullptr, ETeleportType::TeleportPhysics);
						}
					}

					UChildActorComponent* ChildActorComp = Cast<UChildActorComponent>(Component);
					if (ChildActorComp)
					{
						AActor* ChildActor = ChildActorComp->GetChildActor();
						if (ChildActor)
						{
							if (!HasSaveInterface(ChildActor))
							{
								SerializeFromBinary(ChildActor, ComponentArray.Data);
							}
						}
					}
					else
					{
						SerializeFromBinary(Component, ComponentArray.Data);
					}

					//UE_LOG(LogEasyMultiSave, Log, TEXT("Component Loaded: %s"), *Component->GetName());	
				}
			}
		}
	}
}

/**
Serialize Functions
**/

void UEMSObject::SaveActorToBinary(AActor* Actor, FGameObjectSaveData& OutData)
{
	IEMSActorSaveInterface::Execute_ActorPreSave(Actor);

	Actor->Tags.Remove(HasLoadedTag);

	SerializeToBinary(Actor, OutData.Data);

	if (GetActorType(Actor) != EActorType::AT_LevelScript)
	{
		SaveActorComponents(Actor, OutData.Components); 
	}

	IEMSActorSaveInterface::Execute_ActorSaved(Actor);
}

void UEMSObject::LoadActorFromBinary(AActor* Actor, const FGameObjectSaveData& InData)
{
	Actor->Tags.Add(HasLoadedTag);

	SerializeFromBinary(Actor, InData.Data);

	if (GetActorType(Actor) != EActorType::AT_LevelScript)
	{
		LoadActorComponents(Actor, InData.Components);
	}

	IEMSActorSaveInterface::Execute_ActorLoaded(Actor); //Post Component Load
}

void UEMSObject::SerializeToBinary(UObject* Object, TArray<uint8>& OutData)
{
	FMemoryWriter MemoryWriter(OutData, true);
	FSaveGameArchive Ar(MemoryWriter);
	Object->Serialize(Ar);

	MemoryWriter.FlushCache();
	MemoryWriter.Close();
}

void UEMSObject::SerializeFromBinary(UObject* Object, const TArray<uint8>& InData)
{
	FMemoryReader MemoryReader(InData, true);
	FSaveGameArchive Ar(MemoryReader);
	Object->Serialize(Ar);

	MemoryReader.FlushCache();
	MemoryReader.Close();
}

/**
Blueprint Struct Property Serialization
This is the "easy way out" for BP structs, without engine change. See FProperty::ShouldSerializeValue
**/

void UEMSObject::SerializeActorStructProperties(AActor* Actor)
{
	SerializeStructProperties(Actor);

	//Also for Components
	TArray<UActorComponent*> SourceComps;
	IEMSActorSaveInterface::Execute_ComponentsToSave(Actor, SourceComps);

	if (SourceComps.Num() > 0)
	{
		for (UActorComponent* Component : SourceComps)
		{
			if (Component)
			{
				SerializeStructProperties(Component);
			}
		}
	}
}

void UEMSObject::SerializeStructProperties(UObject* Object)
{
	//Non-array struct vars.
	for (TFieldIterator<FStructProperty> ObjectStruct(Object->GetClass()); ObjectStruct; ++ObjectStruct)
	{
		if (ObjectStruct && ObjectStruct->GetPropertyFlags() & CPF_SaveGame)
		{
			SerializeScriptStruct(ObjectStruct->Struct);
		}
	}

	//Struct-Arrays are cast as Arrays, not structs, so we work around it.
	for (TFieldIterator<FArrayProperty> ArrayProp(Object->GetClass()); ArrayProp; ++ArrayProp)
	{
		if (ArrayProp && ArrayProp->GetPropertyFlags() & CPF_SaveGame)
		{
			SerializeArrayStruct(*ArrayProp);
		}
	}

	//Map Properties
	for (TFieldIterator<FMapProperty> MapProp(Object->GetClass()); MapProp; ++MapProp)
	{
		if (MapProp && MapProp->GetPropertyFlags() & CPF_SaveGame)
		{
			SerializeMap(*MapProp);
		}
	}
}

void UEMSObject::SerializeMap(FMapProperty* MapProp)
{
	FProperty* ValueProp = MapProp->ValueProp;

	if (ValueProp)
	{
		ValueProp->SetPropertyFlags(CPF_SaveGame);

		FStructProperty* ValueStructProp = CastField<FStructProperty>(ValueProp);
		if (ValueStructProp)
		{
			SerializeScriptStruct(ValueStructProp->Struct);
		}
	}
}

void UEMSObject::SerializeArrayStruct(FArrayProperty* ArrayProp)
{
	FProperty* InnerProperty = ArrayProp->Inner;
	if (InnerProperty)
	{
		//Here we finally get to the structproperty, wich hides in the Array->Inner
		FStructProperty* ArrayStructProp = CastField<FStructProperty>(InnerProperty);
		if (ArrayStructProp)
		{
			SerializeScriptStruct(ArrayStructProp->Struct);
			//UE_LOG(LogEasyMultiSave, Warning, TEXT("name %s"), *ArrayStructProp->GetName());
		}
	}
}

void UEMSObject::SerializeScriptStruct(UStruct* ScriptStruct)
{
	if (ScriptStruct)
	{
		for (TFieldIterator<FProperty> Prop(ScriptStruct); Prop; ++Prop)
		{
			if (Prop)
			{
				//UE_LOG(LogEasyMultiSave, Warning, TEXT("name %s"), *Prop->GetName());
				Prop->SetPropertyFlags(CPF_SaveGame);

				//Recursive Array
				FArrayProperty* ArrayProp = CastField<FArrayProperty>(*Prop);
				if (ArrayProp)
				{
					SerializeArrayStruct(ArrayProp);
				}

				//Recursive Struct
				FStructProperty* StructProp = CastField<FStructProperty>(*Prop);
				if (StructProp)
				{
					SerializeScriptStruct(StructProp->Struct);
				}

				//Recursive Map
				FMapProperty* MapProp = CastField<FMapProperty>(*Prop);
				if (MapProp)
				{
					SerializeMap(MapProp);
				}
			}
		}
	}
}

/**
Helper Functions
**/

bool UEMSObject::HasSaveInterface(AActor* Actor)
{
	return Actor->GetClass()->ImplementsInterface(UEMSActorSaveInterface::StaticClass());
}

bool UEMSObject::IsValidActor(AActor* Actor)
{
	return !Actor->IsPendingKill() && HasSaveInterface(Actor);
}

bool UEMSObject::IsValidForSaving(AActor* Actor)
{
	return IsValidActor(Actor) && !Actor->ActorHasTag(SkipSaveTag);
}

bool UEMSObject::IsValidForLoading(AActor* Actor)
{
	return IsValidActor(Actor) && !Actor->ActorHasTag(HasLoadedTag);
}

EActorType UEMSObject::GetActorType(AActor* Actor)
{
	//Non controlled pawns are saved in the level.
	const APawn* Pawn = Cast<APawn>(Actor);
	if (Pawn)
	{
		if (Pawn->IsPlayerControlled())
		{
			return EActorType::AT_Player;
		}
		else
		{
			//Persistent Actors
			if (Pawn->ActorHasTag(PersistentTag))
			{
				return EActorType::AT_Persistent;
			}

			return EActorType::AT_Runtime;
		}
	}

	if (Cast<APlayerController>(Actor) || Cast<APlayerState>(Actor))
	{
		return EActorType::AT_Player;
	}

	if (Cast<ALevelScriptActor>(Actor))
	{
		return EActorType::AT_LevelScript;
	}

	if (Cast<AGameModeBase>(Actor) || Cast<AGameStateBase>(Actor))
	{
		return EActorType::AT_GameObject;
	}

	if (Actor->ActorHasTag(PersistentTag))
	{
		return EActorType::AT_Persistent;
	}

	//Set to placed if the actor was already there on level/sub-level load. Just skips saving ClassName, nothing more
	if (Actor->IsNetStartupActor())
	{
		return EActorType::AT_Placed;
	}

	return EActorType::AT_Runtime;
}

FName UEMSObject::GetLevelName()
{
	//Get full path without PIE prefixes

	FString LevelName = GetWorld()->GetOuter()->GetName();
	const FString Prefix = GetWorld()->StreamingLevelsPrefix;

	int Index = LevelName.Find(Prefix);
	int Count = Prefix.Len();

	LevelName.RemoveAt(Index, Count);

	//UE_LOG(LogTemp, Log, TEXT("Level Name:  %s "), *LevelName);

	return FName(*LevelName);
}

TArray<uint8> UEMSObject::BytesFromString(const FString& String)
{
	const uint32 Size = String.Len();

	TArray<uint8> Bytes;
	Bytes.AddUninitialized(Size);
	StringToBytes(String, Bytes.GetData(), Size);

	return Bytes;
}

FString UEMSObject::StringFromBytes(const TArray<uint8>& Bytes)
{
	return BytesToString(Bytes.GetData(), Bytes.Num());
}

bool UEMSObject::IsMovable(USceneComponent* SceneComp)
{
	if (SceneComp != nullptr)
	{
		return SceneComp->Mobility == EComponentMobility::Movable;
	}

	return false;
}

APlayerController* UEMSObject::GetPlayerController()
{
	return UGameplayStatics::GetPlayerController(GetWorld(), PlayerIndex);
}

APawn* UEMSObject::GetPlayerPawn()
{
	if (APlayerController* PC = GetPlayerController())
	{
		return PC->GetPawnOrSpectator();
	}

	return nullptr;
}

FTimerManager& UEMSObject::GetTimerManager()
{
	return GetWorld()->GetTimerManager();
}

bool UEMSObject::IsAsyncSaveOrLoadTaskActive(const ESaveGameMode& Mode, bool bLog)
{
	//This will prevent the functions from being executed at all during pause
	if (bLog)
	{
		if (GetWorld()->IsPaused())
		{
			UE_LOG(LogEasyMultiSave, Warning, TEXT(" Async save or load called during pause. Operation was canceled."));
			return true;
		}
	}

	for (TObjectIterator<UEMSAsyncLoadGame> It; It; ++It)
	{
		if (*It && It->bIsActive && (It->Mode == Mode || It->Mode == ESaveGameMode::MODE_All))
		{
			if (bLog)
			{
				UE_LOG(LogEasyMultiSave, Warning, TEXT(" 'Load Game Actors' is active while trying to save or load."));
			}

			return true;
		}
	}

	for (TObjectIterator<UEMSAsyncSaveGame> It; It; ++It)
	{
		if (*It && It->bIsActive && (It->Mode == Mode || It->Mode == ESaveGameMode::MODE_All))
		{
			if (bLog)
			{
				UE_LOG(LogEasyMultiSave, Warning, TEXT(" 'Save Game Actors' is active while trying to save or load."));
			}

			return true;
		}
	}

	return false;
}

bool UEMSObject::HasValidGameMode()
{
	const AGameModeBase* GameMode = GetWorld()->GetAuthGameMode();
	return IsValid(GameMode);
}

bool UEMSObject::HasValidPlayer()
{
	return IsValid(GetPlayerPawn());
}

/**
Template Functions
**/

template <class TSaveGame>
TSaveGame* UEMSObject::GetDesiredSaveObject(const FString& FullSavePath, const TSubclassOf<TSaveGame>& Class, TSaveGame*& SaveGameObject)
{
	if (FullSavePath.IsEmpty())
	{
		return nullptr;
	}

	//If we don't have a cached one, we load it and set the cached one
	if (!SaveGameObject)
	{
		USaveGame* SaveGame = LoadObject(FullSavePath, Class);
		SaveGameObject = Cast<TSaveGame>(SaveGame);
	}

	if (!SaveGameObject)
	{
		UE_LOG(LogEasyMultiSave, Warning, TEXT("Invalid Save Game Object: %s"), *FullSavePath);
		return nullptr;
	}

	return SaveGameObject;
}

/**
Thumbnail Saving
Simple saving as .png from a 2d scene capture render target source.
**/

UTexture2D* UEMSObject::ImportSaveThumbnail(const FString& SaveGameName)
{
	FString SaveThumbnailName = ThumbnailSaveFile(SaveGameName);

	//Suppress warning messages when we dont have a thumb yet.
	if (FPaths::FileExists(SaveThumbnailName))
	{
		return FImageUtils::ImportFileAsTexture2D(SaveThumbnailName);
	}
	
	return nullptr;
}

void UEMSObject::ExportSaveThumbnail(UTextureRenderTarget2D* TextureRenderTarget, const FString& SaveGameName)
{
	
	FString SaveThumbnailName = ThumbnailSaveFile(SaveGameName);
	FText PathError;

	if (!TextureRenderTarget)
	{
		UE_LOG(LogEasyMultiSave, Warning, TEXT("ExportSaveThumbnailRT: TextureRenderTarget must be non-null"));
	}
	else if (!TextureRenderTarget->Resource)
	{
		UE_LOG(LogEasyMultiSave, Warning, TEXT("ExportSaveThumbnailRT: Render target has been released"));
	}
	else if (!PathError.IsEmpty())
	{
		UE_LOG(LogEasyMultiSave, Warning, TEXT("ExportSaveThumbnailRT: Invalid file path provided: %s"), *PathError.ToString());
	}
	else if (SaveGameName.IsEmpty())
	{
		UE_LOG(LogEasyMultiSave, Warning, TEXT("ExportSaveThumbnailRT: FileName must be non-empty"));
	}
	else
	{
		FArchive* Ar = IFileManager::Get().CreateFileWriter(*SaveThumbnailName);

		if (Ar)
		{
			FBufferArchive Buffer;

			bool bSuccess = FImageUtils::ExportRenderTarget2DAsPNG(TextureRenderTarget, Buffer);

			if (bSuccess)
			{
				Ar->Serialize(const_cast<uint8*>(Buffer.GetData()), Buffer.Num());
			}

			delete Ar;
		}
		else
		{
			UE_LOG(LogEasyMultiSave, Warning, TEXT("ExportSaveThumbnailRT: FileWrite failed to create"));
		}
	}
	
}


