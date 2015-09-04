// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	InputSettings.cpp: Project configurable input settings
=============================================================================*/

#include "EnginePrivate.h"

#if WITH_EDITOR
#include "UnrealEd.h"
#endif
#include "GameFramework/InputSettings.h"

UInputSettings::UInputSettings(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

void UInputSettings::PostInitProperties()
{
	Super::PostInitProperties();

	if (ConsoleKey_DEPRECATED.IsValid())
	{
		ConsoleKeys.Empty(1);
		ConsoleKeys.Add(ConsoleKey_DEPRECATED);
	}

	TMap<FName, int32> UniqueAxisConfigNames;
	for (int32 Index = 0; Index < AxisConfig.Num(); ++Index)
	{
		UniqueAxisConfigNames.Add(AxisConfig[Index].AxisKeyName, Index);
	}

	for (int32 Index = AxisConfig.Num() - 1; Index >= 0; --Index)
	{
		const int32 UniqueAxisIndex = UniqueAxisConfigNames.FindChecked(AxisConfig[Index].AxisKeyName);
		if (UniqueAxisIndex != Index)
		{
			AxisConfig.RemoveAtSwap(Index);
		}
	}

#if PLATFORM_WINDOWS
	// If the console key is set to the default we'll see about adding the keyboard default
	// If they've mapped any additional keys, we'll just assume they've set it up in a way they desire
	if (ConsoleKeys.Num() == 1 && ConsoleKeys[0] == EKeys::Tilde)
	{
		FKey DefaultConsoleKey = EKeys::Tilde;
		switch(PRIMARYLANGID(LOWORD(GetKeyboardLayout(0))))
		{
		case LANG_FRENCH:
			DefaultConsoleKey = FInputKeyManager::Get().GetKeyFromCodes(VK_OEM_7, 0);
			break;

		case LANG_GERMAN:
			DefaultConsoleKey = EKeys::Caret;
			break;

		case LANG_SPANISH:
			DefaultConsoleKey = FInputKeyManager::Get().GetKeyFromCodes(VK_OEM_5, 0);
			break;
			
		case LANG_JAPANESE:
		case LANG_RUSSIAN:
			DefaultConsoleKey = FInputKeyManager::Get().GetKeyFromCodes(VK_OEM_3, 0);
			break;
		}

		if (DefaultConsoleKey != EKeys::Tilde && DefaultConsoleKey.IsValid())
		{
			ConsoleKeys.Add(DefaultConsoleKey);
		}
	}
#endif
}

#if WITH_EDITOR
void UInputSettings::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	const FName MemberPropertyName = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue()->GetFName();

	if (MemberPropertyName == "ActionMappings" || MemberPropertyName == "AxisMappings" || MemberPropertyName == "AxisConfig")
	{
		ForceRebuildKeymaps();
		FEditorDelegates::OnActionAxisMappingsChanged.Broadcast();
	}
}

#endif

void UInputSettings::SaveKeyMappings()
{
	ActionMappings.Sort();
	AxisMappings.Sort();
	SaveConfig();
}

void UInputSettings::AddActionMapping(const FInputActionKeyMapping& KeyMapping)
{
	ActionMappings.AddUnique(KeyMapping);
	ForceRebuildKeymaps();
}

void UInputSettings::RemoveActionMapping(const FInputActionKeyMapping& KeyMapping)
{
	for (int32 ActionIndex = ActionMappings.Num() - 1; ActionIndex >= 0; --ActionIndex)
	{
		if (ActionMappings[ActionIndex] == KeyMapping)
		{
			ActionMappings.RemoveAtSwap(ActionIndex);
			// we don't break because the mapping may have been in the array twice
		}
	}
	ForceRebuildKeymaps();
}

void UInputSettings::AddAxisMapping(const FInputAxisKeyMapping& KeyMapping)
{
	AxisMappings.AddUnique(KeyMapping);
	ForceRebuildKeymaps();
}

void UInputSettings::RemoveAxisMapping(const FInputAxisKeyMapping& InKeyMapping)
{
	for (int32 AxisIndex = AxisMappings.Num() - 1; AxisIndex >= 0; --AxisIndex)
	{
		const FInputAxisKeyMapping& KeyMapping = AxisMappings[AxisIndex];
		if (KeyMapping.AxisName == InKeyMapping.AxisName
			&& KeyMapping.Key == InKeyMapping.Key)
		{
			AxisMappings.RemoveAtSwap(AxisIndex);
			// we don't break because the mapping may have been in the array twice
		}
	}
	ForceRebuildKeymaps();
}

void UInputSettings::GetActionNames(TArray<FName>& ActionNames) const
{
	ActionNames.Empty();

	for (const FInputActionKeyMapping& ActionMapping : ActionMappings)
	{
		ActionNames.AddUnique(ActionMapping.ActionName);
	}
}

void UInputSettings::GetAxisNames(TArray<FName>& AxisNames) const
{
	AxisNames.Empty();

	for (const FInputAxisKeyMapping& AxisMapping : AxisMappings)
	{
		AxisNames.AddUnique(AxisMapping.AxisName);
	}	
}

void UInputSettings::ForceRebuildKeymaps()
{
	for (TObjectIterator<UPlayerInput> It; It; ++It)
	{
		It->ForceRebuildingKeyMaps(true);
	}
}