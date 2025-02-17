//  
// Copyright (c) 2023 articy Software GmbH & Co. KG. All rights reserved.  
//

#include "ArticyEditorModule.h"
#include "ArticyPluginSettings.h"
#include "ArticyEditorCommands.h"
#include "ArticyEditorFunctionLibrary.h"
#include "ArticyEditorStyle.h"
#include "ArticyFlowClasses.h"
#include "CodeGeneration/CodeGenerator.h"
#include "Customizations/ArticyIdPropertyWidgetCustomizations/DefaultArticyIdPropertyWidgetCustomizations.h"
#include "Developer/Settings/Public/ISettingsModule.h"
#include "Developer/Settings/Public/ISettingsSection.h"
#include "Editor/PropertyEditor/Public/PropertyEditorModule.h"
#include "Misc/MessageDialog.h"
#include "Dialogs/Dialogs.h"
#include <Widgets/SWindow.h>
#include "AssetToolsModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Editor.h"
#include "DirectoryWatcherModule.h"
#include "HAL/FileManager.h"
#include "Widgets/Images/SImage.h"
#include "IDirectoryWatcher.h"
#include "Customizations/ArticyPinFactory.h"
#include "Customizations/AssetActions/AssetTypeActions_ArticyGv.h"
#include "Customizations/AssetActions/AssetTypeActions_ArticyAlterativeGV.h"
#include "Customizations/Details/ArticyGVCustomization.h"
#include "Customizations/Details/ArticyPluginSettingsCustomization.h"
#include "Customizations/Details/ArticyIdCustomization.h"
#include "Customizations/Details/ArticyRefCustomization.h"
#include "Slate/GV/SArticyGlobalVariablesDebugger.h"

#if ENGINE_MAJOR_VERSION >= 5
// In UE5, you use the ToolMenus API to extend the UI
#include "ToolMenus.h"
#else
// Otherwise, we have to jack into the level editor module and build a new button in
#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#endif

DEFINE_LOG_CATEGORY(LogArticyEditor)

#define LOCTEXT_NAMESPACE "FArticyImporterModule"
static const FName ArticyWindowTabID("ArticyWindowTab");
static const FName ArticyGVDebuggerTabID("ArticyGVDebuggerTab");

/**
 * Initialize the Articy editor module by registering customizations, commands, and toolbars.
 */
void FArticyEditorModule::StartupModule()
{
	CustomizationManager = MakeShareable(new FArticyEditorCustomizationManager);

	RegisterAssetTypeActions();
	RegisterConsoleCommands();
	RegisterDefaultArticyIdPropertyWidgetExtensions();
	RegisterDetailCustomizations();
	RegisterGraphPinFactory();
	RegisterPluginSettings();
	RegisterPluginCommands();
	RegisterArticyToolbar();
	// directory watcher has to be changed or removed as the results aren't quite deterministic
	//RegisterDirectoryWatcher();
	RegisterToolTabs();

	FArticyEditorStyle::Initialize();
}

/**
 * Clean up the Articy editor module by unregistering settings and destroying console commands.
 */
void FArticyEditorModule::ShutdownModule()
{
	if (UObjectInitialized())
	{
		GetCustomizationManager()->Shutdown();
		UnregisterPluginSettings();

		if (ConsoleCommands != nullptr)
		{
			delete ConsoleCommands;
			ConsoleCommands = nullptr;
		}
	}
}

/**
 * Register a directory watcher to monitor changes in the generated code directory.
 */
void FArticyEditorModule::RegisterDirectoryWatcher()
{
	FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
	DirectoryWatcherModule.Get()->RegisterDirectoryChangedCallback_Handle(CodeGenerator::GetSourceFolder(), IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FArticyEditorModule::OnGeneratedCodeChanged), GeneratedCodeWatcherHandle);
}

/**
 * Register a custom graph pin factory for Articy references.
 */
void FArticyEditorModule::RegisterGraphPinFactory() const
{
	TSharedPtr<FArticyRefPinFactory> ArticyRefPinFactory = MakeShareable(new FArticyRefPinFactory);
	FEdGraphUtilities::RegisterVisualPinFactory(ArticyRefPinFactory);
}

/**
 * Register console commands for the Articy editor module.
 */
void FArticyEditorModule::RegisterConsoleCommands()
{
	ConsoleCommands = new FArticyEditorConsoleCommands(*this);
}

/**
 * Register default Articy ID property widget extensions for Windows platforms.
 */
void FArticyEditorModule::RegisterDefaultArticyIdPropertyWidgetExtensions() const
{
#if PLATFORM_WINDOWS
	// this registers the articy button extension for all UArticyObjects. Only for Windows, since articy is only available for windows
	GetCustomizationManager()->RegisterArticyIdPropertyWidgetCustomizationFactory(FOnCreateArticyIdPropertyWidgetCustomizationFactory::CreateLambda([]()
		{
			return MakeShared<FArticyButtonCustomizationFactory>();
		}));
#endif
}

/**
 * Register detail customizations for Articy properties and settings.
 */
void FArticyEditorModule::RegisterDetailCustomizations() const
{
	// register custom details for ArticyRef struct
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.RegisterCustomPropertyTypeLayout("ArticyId", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FArticyIdCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("ArticyRef", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FArticyRefCustomization::MakeInstance));
	PropertyModule.RegisterCustomClassLayout("ArticyPluginSettings", FOnGetDetailCustomizationInstance::CreateStatic(&FArticyPluginSettingsCustomization::MakeInstance));
	PropertyModule.RegisterCustomClassLayout("ArticyGlobalVariables", FOnGetDetailCustomizationInstance::CreateStatic(&FArticyGVCustomization::MakeInstance));

	PropertyModule.NotifyCustomizationModuleChanged();
}

/**
 * Retrieve all Articy packages in the project, searching through asset data.
 *
 * @return An array of Articy packages.
 */
TArray<UArticyPackage*> FArticyEditorModule::GetPackagesSlow()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> PackageData;

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >0
	AssetRegistryModule.Get().GetAssetsByClass(UArticyGlobalVariables::StaticClass()->GetClassPathName(), PackageData);
#else
	AssetRegistryModule.Get().GetAssetsByClass(UArticyPackage::StaticClass()->GetFName(), PackageData);
#endif	

	TArray<UArticyPackage*> Packages;
	for (const FAssetData& Data : PackageData)
	{
		Packages.Add(Cast<UArticyPackage>(Data.GetAsset()));
	}

	return Packages;
}

/**
 * Register the Articy toolbar, adding custom buttons for Articy utilities.
 */
void FArticyEditorModule::RegisterArticyToolbar()
{
#if ENGINE_MAJOR_VERSION >= 5
	// Grab the appropriate toolbar menu so we can extend it
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.LevelToolbarQuickSettings");

	{
		// Create a new section for Articy utilities
		FToolMenuSection& Section = Menu->AddSection("ArticyUtilities", LOCTEXT("ArticyUtilities", "Articy Utilities"));

		// Add buttons
		Section.AddMenuEntryWithCommandList(FArticyEditorCommands::Get().OpenArticyImporter, PluginCommands);
		Section.AddMenuEntryWithCommandList(FArticyEditorCommands::Get().OpenArticyGvDebugger, PluginCommands);
	}
#else 
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	{
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
		ToolbarExtender->AddToolBarExtension("Settings", EExtensionHook::After, PluginCommands, FToolBarExtensionDelegate::CreateRaw(this, &FArticyEditorModule::AddToolbarExtension));
		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
	}
#endif
}

// Old toolbar code for UE4
#if ENGINE_MAJOR_VERSION == 4
void FArticyEditorModule::AddToolbarExtension(FToolBarBuilder& Builder)
{
	Builder.AddComboButton(FUIAction(), FOnGetContent::CreateRaw(this, &FArticyEditorModule::OnGenerateArticyToolsMenu), FText::FromString(TEXT("Articy Tools")), TAttribute<FText>(), FSlateIcon(FArticyEditorStyle::GetStyleSetName(), "ArticyImporter.ArticyImporter.40"));
	//Builder.AddToolBarButton(FArticyEditorCommands::Get().OpenPluginWindow, NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FArticyEditorStyle::GetStyleSetName(), "ArticyImporter.ArticyImporter.40") );
}

TSharedRef<SWidget> FArticyEditorModule::OnGenerateArticyToolsMenu() const
{
	FMenuBuilder MenuBuilder(true, PluginCommands);

	MenuBuilder.BeginSection("ArticyTools", LOCTEXT("ArticyTools", "Articy Tools"));
	MenuBuilder.AddMenuEntry(FArticyEditorCommands::Get().OpenArticyImporter);
	MenuBuilder.AddMenuEntry(FArticyEditorCommands::Get().OpenArticyGVDebugger);
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}
#endif

/**
 * Register asset type actions for Articy global variables.
 */
void FArticyEditorModule::RegisterAssetTypeActions()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_ArticyGv()));
	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_ArticyAlterativeGV()));
}

/**
 * Register plugin commands for opening the Articy importer and debugger.
 */
void FArticyEditorModule::RegisterPluginCommands()
{
	FArticyEditorCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(FArticyEditorCommands::Get().OpenArticyImporter,
		FExecuteAction::CreateRaw(this, &FArticyEditorModule::OpenArticyWindow),
		FCanExecuteAction());

	PluginCommands->MapAction(FArticyEditorCommands::Get().OpenArticyGvDebugger,
		FExecuteAction::CreateRaw(this, &FArticyEditorModule::OpenArticyGVDebugger),
		FCanExecuteAction());
}

/**
 * Register tool tabs for the Articy editor, including the main menu and debugger.
 */
void FArticyEditorModule::RegisterToolTabs()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ArticyWindowTabID, FOnSpawnTab::CreateRaw(this, &FArticyEditorModule::OnSpawnArticyMenuTab))
		.SetDisplayName(LOCTEXT("ArticyWindowTitle", "Articy Menu"))
		.SetIcon(FSlateIcon(FArticyEditorStyle::GetStyleSetName(), "ArticyImporter.ArticyImporter.16", "ArticyImporter.ArticyImporter.8"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ArticyGVDebuggerTabID, FOnSpawnTab::CreateRaw(this, &FArticyEditorModule::OnSpawnArticyGVDebuggerTab))
		.SetDisplayName(LOCTEXT("ArticyGVDebuggerTitle", "Articy GV Debugger"))
		.SetIcon(FSlateIcon(FArticyEditorStyle::GetStyleSetName(), "ArticyImporter.ArticyImporter.16", "ArticyImporter.ArticyImporter.8"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

/**
 * Register plugin settings for the Articy editor in the project settings.
 */
void FArticyEditorModule::RegisterPluginSettings() const
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

	if (SettingsModule != nullptr)
	{
		ISettingsSectionPtr SettingsSectionPtr = SettingsModule->RegisterSettings("Project", "Plugins", "ArticyImporter",
			LOCTEXT("Name", "Articy X Importer"),
			LOCTEXT("Description", "Articy X Importer Configuration."),
			GetMutableDefault<UArticyPluginSettings>()
		);
	}
}

/**
 * Unregister plugin settings for the Articy editor in the project settings.
 */
void FArticyEditorModule::UnregisterPluginSettings() const
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "ArticyImporter");
	}
}

/**
 * Check if an import is currently queued.
 *
 * @return True if an import is queued, false otherwise.
 */
bool FArticyEditorModule::IsImportQueued()
{
	return bIsImportQueued;
}

/**
 * Queue an import operation, displaying a message if in play mode.
 */
void FArticyEditorModule::QueueImport()
{
	bIsImportQueued = true;
	FOnMsgDlgResult OnDialogClosed;
	FText Message = LOCTEXT("ImportWhilePlaying", "To import articy:draft data, the play mode has to be quit. Import will begin after exiting play.");
	FText Title = LOCTEXT("ImportWhilePlaying_Title", "Import not possible");
	TSharedRef<SWindow> Window = OpenMsgDlgInt_NonModal(EAppMsgType::Ok, Message, Title, OnDialogClosed);
	Window->BringToFront(true);
	QueuedImportHandle = FEditorDelegates::EndPIE.AddRaw(this, &FArticyEditorModule::TriggerQueuedImport);
}

/**
 * Open the Articy window tab.
 */
void FArticyEditorModule::OpenArticyWindow()
{
	// @TODO Engine versioning
#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION < 26
	FGlobalTabmanager::Get()->InvokeTab(ArticyWindowTabID);
#else
	FGlobalTabmanager::Get()->TryInvokeTab(ArticyWindowTabID);
#endif
}

/**
 * Open the Articy global variables debugger tab.
 */
void FArticyEditorModule::OpenArticyGVDebugger()
{
	// @TODO Engine versioning
#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION < 26
	FGlobalTabmanager::Get()->InvokeTab(ArticyGVDebuggerTabID);
#else
	FGlobalTabmanager::Get()->TryInvokeTab(ArticyGVDebuggerTabID);
#endif
}

/**
 * Check the validity of the import status, verifying the presence of required assets and files.
 *
 * @return The status of the import validity.
 */
EImportStatusValidity FArticyEditorModule::CheckImportStatusValidity() const
{
	UArticyImportData* ImportData = nullptr;
	FArticyEditorFunctionLibrary::EnsureImportDataAsset(&ImportData);

	if (!ImportData)
	{
		return EImportStatusValidity::ImportDataAssetMissing;
	}
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(FileNames, *CodeGenerator::GetSourceFolder());

	// if we have less than 5 code files we are missing at least one
	if (FileNames.Num() < 5)
	{
		return EImportStatusValidity::FileMissing;
	}

	TArray<FAssetData> ArticyAssets;
	AssetRegistryModule.Get().GetAssetsByPath(FName(*ArticyHelpers::GetArticyGeneratedFolder()), ArticyAssets, true);

	// check if all assets are actually valid (classes not found would result in a nullptr)
	for (const FAssetData& Data : ArticyAssets)
	{
		UObject* Asset = Data.GetAsset();

		if (!Asset)
		{
			// if the asset exists but is invalid, the class is probably missing
			return EImportStatusValidity::FileMissing;
		}
	}

	// if we have less than 3 assets that means we have no package, no database or no global variables
	if (ArticyAssets.Num() < 3)
	{
		return EImportStatusValidity::ImportantAssetMissing;
	}

	return EImportStatusValidity::Valid;
}

/**
 * Handle changes to generated code files and prompt for a full reimport if necessary.
 *
 * @param FileChanges Array of file change data.
 */
void FArticyEditorModule::OnGeneratedCodeChanged(const TArray<FFileChangeData>& FileChanges) const
{
	const EImportStatusValidity Validity = CheckImportStatusValidity();

	// only check for missing files, as the code changes mid-import process too and we'd need to manage state if we wanted to check for assets as well when code changes
	if (Validity == EImportStatusValidity::FileMissing)
	{
		FText Message = FText::FromString(TEXT("It appears a generated code file is missing. Perform full reimport now?"));
		FText Title = FText::FromString(TEXT("Articy detected an error"));
#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION <= 24
		EAppReturnType::Type ReturnType = OpenMsgDlgInt(EAppMsgType::YesNo, Message, Title);
#elif ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
		EAppReturnType::Type ReturnType = FMessageDialog::Open(EAppMsgType::YesNo, Message, Title);
#else
		EAppReturnType::Type ReturnType = FMessageDialog::Open(EAppMsgType::YesNo, Message, &Title);
#endif

		if (ReturnType == EAppReturnType::Yes)
		{
			FArticyEditorFunctionLibrary::ForceCompleteReimport();
		}
	}
}

/**
 * Unqueue a pending import operation.
 */
void FArticyEditorModule::UnqueueImport()
{
	FEditorDelegates::EndPIE.Remove(QueuedImportHandle);
	QueuedImportHandle.Reset();
	bIsImportQueued = false;
}

/**
 * Trigger a queued import operation when exiting play mode.
 *
 * @param b Indicates whether the import should proceed.
 */
void FArticyEditorModule::TriggerQueuedImport(bool b)
{
	FArticyEditorFunctionLibrary::ReimportChanges();
	// important to unqueue in the end to reset the state
	UnqueueImport();
}

/**
 * Spawn the Articy menu tab, providing UI for reimporting and regenerating assets.
 *
 * @param SpawnTabArgs The arguments for spawning the tab.
 * @return The created dock tab widget.
 */
TSharedRef<SDockTab> FArticyEditorModule::OnSpawnArticyMenuTab(const FSpawnTabArgs& SpawnTabArgs) const
{
	float ButtonWidth = 333.f / 1.3f;
	float ButtonHeight = 101.f / 1.3f;
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.VAlign(VAlign_Bottom)
						.HAlign(HAlign_Center)
						.Padding(10.f)
						[
							SNew(SImage)
								.Image(FArticyEditorStyle::Get().GetBrush("ArticyImporter.Window.ImporterLogo"))
						]
						+ SVerticalBox::Slot()
						.VAlign(VAlign_Top)
						.HAlign(HAlign_Center)
						[
							SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Center)
								[
									SNew(SBox)
										.WidthOverride(ButtonWidth)
										.HeightOverride(ButtonHeight)
										[
											SNew(SButton)
												.ButtonStyle(FArticyEditorStyle::Get(), "ArticyImporter.Button.FullReimport")
												.ToolTipText(LOCTEXT("ForceCompleteReimportTooltip", "Forces a complete reimport of articy draft data including code and asset generation."))
												.OnClicked_Lambda([]() -> FReply { FArticyEditorFunctionLibrary::ForceCompleteReimport(); return FReply::Handled(); })
										]
								]
								+ SHorizontalBox::Slot()
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Center)
								[
									SNew(SBox)
										.WidthOverride(ButtonWidth)
										.HeightOverride(ButtonHeight)
										[
											SNew(SButton)
												.ButtonStyle(FArticyEditorStyle::Get(), "ArticyImporter.Button.ImportChanges")
												.ToolTipText(LOCTEXT("ImportChangesTooltip", "Imports only the changes from last import. This is usually quicker than a complete reimport."))
												.OnClicked_Lambda([]() -> FReply { FArticyEditorFunctionLibrary::ReimportChanges(); return FReply::Handled(); })
										]
								]
								+ SHorizontalBox::Slot()
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Center)
								[
									SNew(SBox)
										.WidthOverride(ButtonWidth)
										.HeightOverride(ButtonHeight)
										[
											SNew(SButton)
												.ButtonStyle(FArticyEditorStyle::Get(), "ArticyImporter.Button.RegenerateAssets")
												.ToolTipText(LOCTEXT("RegenerateAssetsTooltip", "Regenerates all articy assets based on the currently generated code and the import data asset."))
												.OnClicked_Lambda([]() -> FReply { FArticyEditorFunctionLibrary::RegenerateAssets(); return FReply::Handled(); })
										]
								]
						]
				]
				+ SOverlay::Slot()
				.VAlign(VAlign_Bottom)
				.HAlign(HAlign_Right)
				.Padding(5.f)
				[
					SNew(SImage)
						.Image(FArticyEditorStyle::Get().GetBrush("ArticyImporter.Window.ArticyLogo"))
				]
		];
}

/**
 * Spawn the Articy global variables debugger tab.
 *
 * @param SpawnTabArgs The arguments for spawning the tab.
 * @return The created dock tab widget.
 */
TSharedRef<SDockTab> FArticyEditorModule::OnSpawnArticyGVDebuggerTab(const FSpawnTabArgs& SpawnTabArgs) const
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SArticyGlobalVariablesRuntimeDebugger).bInitiallyCollapsed(true)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FArticyEditorModule, ArticyEditor)
