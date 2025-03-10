//  
// Copyright (c) 2023 articy Software GmbH & Co. KG. All rights reserved.  
//

#include "Customizations/Details/ArticyPluginSettingsCustomization.h"
#include "ArticyEditorFunctionLibrary.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "ArticyDatabase.h"
#include "Slate/SPackageSettings.h"
#include "ArticyEditorModule.h"
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >0 
#include "AssetRegistry/AssetRegistryModule.h"
#else
#include "AssetRegistryModule.h"
#endif

#define LOCTEXT_NAMESPACE "ArticyPluginSettings"

/**
 * @brief Default constructor for FArticyPluginSettingsCustomization.
 */
FArticyPluginSettingsCustomization::FArticyPluginSettingsCustomization()
{

}

/**
 * @brief Destructor for FArticyPluginSettingsCustomization.
 *
 * Removes the refresh handle from the ArticyEditorModule's assets generated event.
 */
FArticyPluginSettingsCustomization::~FArticyPluginSettingsCustomization()
{
	// closing the settings window means we no longer want to refresh the UI
	FArticyEditorModule& ArticyEditorModule = FModuleManager::Get().GetModuleChecked<FArticyEditorModule>("ArticyEditor");
	ArticyEditorModule.OnAssetsGenerated.Remove(RefreshHandle);
}

/**
 * @brief Creates a shared instance of FArticyPluginSettingsCustomization.
 *
 * @return A shared pointer to a new instance of FArticyPluginSettingsCustomization.
 */
TSharedRef<IDetailCustomization> FArticyPluginSettingsCustomization::MakeInstance()
{
	return MakeShareable(new FArticyPluginSettingsCustomization);
}

/**
 * @brief Customizes the details panel layout for Articy plugin settings.
 *
 * This function sets up the custom UI for managing Articy package settings.
 *
 * @param DetailLayout The detail layout builder used for customizing the details panel.
 */
void FArticyPluginSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	LayoutBuilder = &DetailLayout;

	// after importing, refresh the custom UI
	FArticyEditorModule& ArticyImporterModule = FModuleManager::Get().GetModuleChecked<FArticyEditorModule>("ArticyEditor");
	RefreshHandle = ArticyImporterModule.OnAssetsGenerated.AddRaw(this, &FArticyPluginSettingsCustomization::RefreshSettingsUI);

	TWeakObjectPtr<const UArticyDatabase> OriginalDatabase = UArticyDatabase::GetMutableOriginal();

	if (!OriginalDatabase.IsValid()) {

		// if there was no database found, check if we are still loading assets; if we are, refresh the custom UI once it's done
		FAssetRegistryModule& AssetRegistry = FModuleManager::Get().GetModuleChecked<FAssetRegistryModule>("AssetRegistry");

		if (AssetRegistry.Get().IsLoadingAssets())
		{
			AssetRegistry.Get().OnFilesLoaded().AddSP(this, &FArticyPluginSettingsCustomization::RefreshSettingsUI);
		}

		return;
	}
	IDetailCategoryBuilder& DefaultPackagesCategory = DetailLayout.EditCategory("Default packages");

	TArray<TSharedPtr<SPackageSettings>> PackageSettingsWidgets;

	// create a custom widget per package
	for (const FString& PackageName : OriginalDatabase->GetImportedPackageNames())
	{
		const FName PackageNameAsName = FName(*PackageName);
		TSharedPtr<SPackageSettings> NewSettingsWidget =
			SNew(SPackageSettings)
			.PackageToDisplay(PackageNameAsName);

		PackageSettingsWidgets.Add(NewSettingsWidget);
	}

	// add the custom widgets to the UI
	for (TSharedPtr<SPackageSettings> PackageSettingsWidget : PackageSettingsWidgets)
	{
		DefaultPackagesCategory.AddCustomRow(LOCTEXT("PackageSetting", ""))
			[
				PackageSettingsWidget.ToSharedRef()
			];
	}
}

/**
 * @brief Refreshes the settings UI by forcing a refresh of the detail layout.
 *
 * This function is called after assets are generated or files are loaded to update the UI.
 */
void FArticyPluginSettingsCustomization::RefreshSettingsUI()
{
	ensure(LayoutBuilder);

	LayoutBuilder->ForceRefreshDetails();
	// the refresh will cause a new instance to be created and used, therefore clear the outdated refresh delegate handle
	FArticyEditorModule& ArticyImporterModule = FModuleManager::Get().GetModuleChecked<FArticyEditorModule>("ArticyEditor");
	ArticyImporterModule.OnImportFinished.Remove(RefreshHandle);
}

#undef LOCTEXT_NAMESPACE
