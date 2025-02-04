// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Linter.h"
#include "ISettingsModule.h"
#include "Framework/Docking/TabManager.h"
#include "LevelEditor.h"
#include "Widgets/Input/SButton.h"
#include "Styling/SlateStyle.h"
#include "AssetRegistryModule.h"
#include "IAssetRegistry.h"
#include "AssetData.h"
#include "ContentBrowserModule.h"
#include "PropertyEditorModule.h"

#include "LinterStyle.h"
#include "LinterContentBrowserExtensions.h"
#include "LinterNamingConvention.h"
#include "LinterSettings.h"
#include "UI/LintWizard.h"
#include "LintRuleSet.h"

#define LOCTEXT_NAMESPACE "FLinterModule"

static const FName LinterTabName = "LinterTab";

void FLinterModule::StartupModule()
{
	// Load the asset registry module
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	if (AssetRegistry.IsLoadingAssets())
	{
		AssetRegistry.OnFilesLoaded().AddRaw(this, &FLinterModule::OnInitialAssetRegistrySearchComplete);
	}
	else
	{
		OnInitialAssetRegistrySearchComplete();
	}

	// Integrate Linter actions into existing editor context menus
	if (!IsRunningCommandlet())
	{
		// Register slate style overrides
		FLinterStyle::Initialize();
		TSharedPtr<FSlateStyleSet> StyleSetPtr = FLinterStyle::StyleSet;

		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->RegisterSettings("Project", "Plugins", "Linter",
				LOCTEXT("RuntimeSettingsName", "Linter"),
				LOCTEXT("RuntimeSettingsDescription", "Configure the Linter plugin"),
				GetMutableDefault<ULinterSettings>());
		}

		// Install UI Hooks
		FLinterContentBrowserExtensions::InstallHooks(this, &ContentBrowserExtenderDelegateHandle, &AssetExtenderDelegateHandle);

		//Register our UI
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			LinterTabName,
			FOnSpawnTab::CreateStatic(&FLinterModule::SpawnTab, StyleSetPtr))
			.SetDisplayName(LOCTEXT("LinterTabName", "Linter"))
			.SetTooltipText(LOCTEXT("LinterTabToolTip", "Linter"))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout(ULinterNamingConvention::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLinterNamingConventionDetails::MakeInstance));
	}

#if WITH_EDITOR
	// Subscribe to event telling us objects are being saved
	FCoreUObjectDelegates::OnObjectPreSave.AddRaw(this, &FLinterModule::OnObjectSaved);
#endif // WITH_EDITOR
}

void FLinterModule::ShutdownModule()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "Linter");
	}

	if (UObjectInitialized())
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(ULinterNamingConvention::StaticClass()->GetFName());

		FLinterContentBrowserExtensions::RemoveHooks(this, &ContentBrowserExtenderDelegateHandle, &AssetExtenderDelegateHandle);

		if (FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
			LevelEditorModule.OnTabManagerChanged().Remove(LevelEditorTabManagerChangedHandle);
		}

		FGlobalTabmanager::Get()->UnregisterTabSpawner(LinterTabName);

		// Unregister slate style overrides
		FLinterStyle::Shutdown();
	}
}

void FLinterModule::Tick(float DeltaTime)
{
	if (SavedObjectPaths.Num() > 0)
	{
		FLinterModule& LinterModule = FModuleManager::LoadModuleChecked<FLinterModule>(TEXT("Linter"));
		LinterModule.SetDesiredLintPaths(SavedObjectPaths);
		FGlobalTabmanager::Get()->FTabManager::TryInvokeTab(FName("LinterTab"));

		SavedObjectPaths.Empty();
	}
}

bool FLinterModule::IsTickable() const
{
	return true;
}

TStatId FLinterModule::GetStatId() const
{
	return 0;
}

TSharedRef<SDockTab> FLinterModule::SpawnTab(const FSpawnTabArgs& TabSpawnArgs, TSharedPtr<FSlateStyleSet> StyleSet)
{
	const FSlateBrush* IconBrush = StyleSet->GetBrush("Linter.Toolbar.Icon");

	const TSharedRef<SDockTab> MajorTab =
		SNew(SDockTab)
		.TabRole(ETabRole::MajorTab);

	MajorTab->SetContent(SNew(SLintWizard));

	return MajorTab;
}

void FLinterModule::OnObjectSaved(UObject* SavedObject, FObjectPreSaveContext SaveContext)
{
	int NumErrors = 0;
	int NumWarnings = 0;

	ULintRuleSet* SelectedLintRuleSet = GetDefault<ULinterSettings>()->DefaultLintRuleSet.LoadSynchronous();
	FLinterModule& LinterModule = FModuleManager::LoadModuleChecked<FLinterModule>(TEXT("Linter"));

	TArray<FString> LintPaths;
	LintPaths.Add(SavedObject->GetPathName());

	FScopedSlowTask SlowTask(0, LOCTEXT("LintingInProgress", "Linting Assets..."));
	SlowTask.MakeDialog(false);

	TArray<TSharedPtr<FLintRuleViolation>> RuleViolations = SelectedLintRuleSet->LintPathShared(LintPaths, &SlowTask);

	for (TSharedPtr<FLintRuleViolation> Violation : RuleViolations)
	{
		if (Violation->ViolatedRule->GetDefaultObject<ULintRule>()->RuleSeverity <= ELintRuleSeverity::Error)
		{
			NumErrors++;
		}
		else
		{
			NumWarnings++;
		}
	}

	if (NumErrors > 0 || NumWarnings > 0)
	{
		SavedObjectPaths.AddUnique(LintPaths[0]);
	}
}

void FLinterModule::OnInitialAssetRegistrySearchComplete()
{
	TryToLoadAllLintRuleSets();
}

void FLinterModule::TryToLoadAllLintRuleSets()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> FoundRuleSets;
	AssetRegistry.GetAssetsByClass(ULintRuleSet::StaticClass()->GetFName(), FoundRuleSets, true);

	// Attempt to get all RuleSets in memory so that linting tools are better aware of them
	for (const FAssetData& RuleSetData : FoundRuleSets)
	{
		if (!RuleSetData.IsAssetLoaded())
		{
			RuleSetData.GetAsset();
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLinterModule, Linter)
DEFINE_LOG_CATEGORY(LogLinter);