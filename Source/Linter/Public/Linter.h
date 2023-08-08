// Copyright 2019-2020 Gamemakin LLC. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

#include "Widgets/Docking/SDockTab.h"
#include "Styling/SlateStyle.h"


#include "UObject/ObjectSaveContext.h"

class FLinterManagerBase;

DECLARE_LOG_CATEGORY_EXTERN(LogLinter, Verbose, All);
DECLARE_LOG_CATEGORY_EXTERN(LogCommandlet, All, All);

class LINTER_API FLinterModule : public IModuleInterface, public FTickableEditorObject
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& TabSpawnArgs, TSharedPtr<FSlateStyleSet> StyleSet);

	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}

	virtual TArray<FString> GetDesiredLintPaths()
	{
		if (DesiredLintPaths.Num() == 0)
		{
			DesiredLintPaths.Push(TEXT("/Game"));
		}

		return DesiredLintPaths;
	}
	virtual void SetDesiredLintPaths(TArray<FString> LintPaths)
	{
		DesiredLintPaths = LintPaths;
		if (DesiredLintPaths.Num() == 0)
		{
			DesiredLintPaths.Push(TEXT("/Game"));
		}
	}

private:
	FDelegateHandle LevelEditorTabManagerChangedHandle;
	FDelegateHandle ContentBrowserExtenderDelegateHandle;
	FDelegateHandle AssetExtenderDelegateHandle;

	TArray<FString> DesiredLintPaths;

	void OnObjectSaved(UObject* objectSaved, FObjectPreSaveContext context);

	TArray<FString> SavedObjectPaths;

public:
	void OnInitialAssetRegistrySearchComplete();
	static void TryToLoadAllLintRuleSets();
};