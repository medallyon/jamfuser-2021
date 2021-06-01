//Easy Multi Save - Copyright (C) 2021 by Michael Hegemann.  

#include "EasyMultiSave.h"
#include "Engine/EngineTypes.h"
#include "Kismet/GameplayStatics.h"
#include "EMSPluginSettings.h"
#include "Developer/Settings/Public/ISettingsModule.h"

#define LOCTEXT_NAMESPACE "FEasyMultiSaveModule"

void FEasyMultiSaveModule::StartupModule()
{
	RegisterSettings();
}

void FEasyMultiSaveModule::ShutdownModule()
{
	if (UObjectInitialized())
	{
		UnregisterSettings();
	}
}

void FEasyMultiSaveModule::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		
		SettingsModule->RegisterSettings("Project", "Plugins", "Easy Multi Save",
			LOCTEXT("RuntimeSettingsName", "Easy Multi Save"),
			LOCTEXT("RuntimeSettingsDescription", "Configure the Easy Multi Save plugin"),
			GetMutableDefault<UEMSPluginSettings>()
		);
		
	}
}

void FEasyMultiSaveModule::UnregisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "Easy Multi Save");
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FEasyMultiSaveModule, EasyMultiSave)