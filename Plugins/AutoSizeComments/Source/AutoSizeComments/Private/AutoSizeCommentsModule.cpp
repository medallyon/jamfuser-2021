// Copyright 2021 fpwong. All Rights Reserved.

#include "AutoSizeCommentsModule.h"

#include "AutoSizeCommentsCacheFile.h"
#include "AutoSizeCommentsGraphPanelNodeFactory.h"
#include "AutoSizeCommentsSettings.h"
#include "ISettingsModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FAutoSizeCommentsModule"

DEFINE_LOG_CATEGORY(LogAutoSizeComments)

class FAutoSizeCommentsModule final : public IAutoSizeCommentsModule
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;

	virtual void ShutdownModule() override;

	virtual FAutoSizeCommentsCacheFile& GetSizeCache() override { return Cache; }

private:
	TSharedPtr<FAutoSizeCommentsGraphPanelNodeFactory> ASCNodeFactory;

	TWeakPtr<SNotificationItem> SuggestedSettingsNotification;

	FAutoSizeCommentsCacheFile Cache;

	void SuggestBlueprintAssistSettings();

	void OnCancelSuggestion();

	void OnApplySuggestion();
};

void FAutoSizeCommentsModule::StartupModule()
{
	// Register the graph node factory
	ASCNodeFactory = MakeShareable(new FAutoSizeCommentsGraphPanelNodeFactory());
	FEdGraphUtilities::RegisterVisualNodeFactory(ASCNodeFactory);

	// Register custom settings to appear in the project settings
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings(
			"Editor", "Plugins", "AutoSizeComments",
			LOCTEXT("AutoSizeCommentsName", "Auto Size Comments"),
			LOCTEXT("AutoSizeCommentsNameDesc", "Configure the Auto Size Comments plugin"),
			GetMutableDefault<UAutoSizeCommentsSettings>()
		);
	}

	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FAutoSizeCommentsModule::SuggestBlueprintAssistSettings);
}

void FAutoSizeCommentsModule::ShutdownModule()
{
	// Remove custom settings
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Editor", "Plugins", "AutoSizeComments");
	}

	// Unregister the graph node factory
	if (ASCNodeFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualNodeFactory(ASCNodeFactory);
		ASCNodeFactory.Reset();
	}

	// Maybe we reloaded the plugin...
	if (SuggestedSettingsNotification.IsValid())
	{
		SuggestedSettingsNotification.Pin()->ExpireAndFadeout();
	}

	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
}

void FAutoSizeCommentsModule::SuggestBlueprintAssistSettings()
{
	if (!FModuleManager::Get().IsModuleLoaded("BlueprintAssist"))
	{
		return;
	}

	UAutoSizeCommentsSettings* MutableSettings = GetMutableDefault<UAutoSizeCommentsSettings>();
	if (MutableSettings->bSuppressSuggestedSettings)
	{
		return;
	}

	const FText Message = FText::FromString("AutoSizeComments: The Blueprint Assist plugin is loaded, apply suggested settings?");
	FNotificationInfo Info(Message);
	Info.bUseSuccessFailIcons = false;
	Info.ExpireDuration = 0.0f;
	Info.FadeInDuration = 0.0f;
	Info.FadeOutDuration = 0.5f;
	Info.bUseThrobber = false;
	Info.bFireAndForget = false;

	Info.ButtonDetails.Add(FNotificationButtonInfo(
		FText::FromString(TEXT("Cancel")),
		FText(),
		FSimpleDelegate::CreateRaw(this, &FAutoSizeCommentsModule::OnCancelSuggestion),
		SNotificationItem::CS_Pending
	));
	
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		FText::FromString(TEXT("Apply")),
		FText(),
		FSimpleDelegate::CreateRaw(this, &FAutoSizeCommentsModule::OnApplySuggestion),
		SNotificationItem::CS_Pending
	));

	Info.CheckBoxState = ECheckBoxState::Checked;
	MutableSettings->Modify();
	MutableSettings->bSuppressSuggestedSettings = true;
	MutableSettings->SaveConfig();

	Info.CheckBoxStateChanged = FOnCheckStateChanged::CreateStatic([](ECheckBoxState NewState)
	{
		UAutoSizeCommentsSettings* MutableSettings = GetMutableDefault<UAutoSizeCommentsSettings>();
		MutableSettings->Modify();
		MutableSettings->bSuppressSuggestedSettings = (NewState == ECheckBoxState::Checked);
		MutableSettings->SaveConfig();
	});

	Info.CheckBoxText = FText::FromString(TEXT("Do not show again"));

	SuggestedSettingsNotification = FSlateNotificationManager::Get().AddNotification(Info);
	SuggestedSettingsNotification.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
}

void FAutoSizeCommentsModule::OnCancelSuggestion()
{
	if (SuggestedSettingsNotification.IsValid())
	{
		SuggestedSettingsNotification.Pin()->ExpireAndFadeout();
	}
}

void FAutoSizeCommentsModule::OnApplySuggestion()
{
	UAutoSizeCommentsSettings* MutableSettings = GetMutableDefault<UAutoSizeCommentsSettings>();
	MutableSettings->Modify();
	MutableSettings->bIgnoreKnotNodes = true;
	MutableSettings->bIgnoreKnotNodesWhenResizing = true;
	MutableSettings->bIgnoreKnotNodesWhenPressingAlt = true;
	MutableSettings->SaveConfig();

	UE_LOG(LogAutoSizeComments, Log, TEXT("Applied suggested settings for Blueprint Assist Module"));
	UE_LOG(LogAutoSizeComments, Log, TEXT("Ignore Knot Nodes: True"));
	UE_LOG(LogAutoSizeComments, Log, TEXT("Ignore Knot Nodes When Resizing: True"));
	UE_LOG(LogAutoSizeComments, Log, TEXT("Ignore Knot Nodes When Pressing Alt: True"));

	if (SuggestedSettingsNotification.IsValid())
	{
		SuggestedSettingsNotification.Pin()->ExpireAndFadeout();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAutoSizeCommentsModule, AutoSizeComments)
