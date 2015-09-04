﻿// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "UnrealEd.h"
#include "SourceCodeNavigation.h"

DEFINE_LOG_CATEGORY_STATIC(LogGatherTextFromMetaDataCommandlet, Log, All);

//////////////////////////////////////////////////////////////////////////
//GatherTextFromMetaDataCommandlet

UGatherTextFromMetaDataCommandlet::UGatherTextFromMetaDataCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UGatherTextFromMetaDataCommandlet::Main( const FString& Params )
{
	// Parse command line - we're interested in the param vals
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	//Set config file
	const FString* ParamVal = ParamVals.Find(FString(TEXT("Config")));
	FString GatherTextConfigPath;
	
	if ( ParamVal )
	{
		GatherTextConfigPath = *ParamVal;
	}
	else
	{
		UE_LOG(LogGatherTextFromMetaDataCommandlet, Error, TEXT("No config specified."));
		return -1;
	}

	//Set config section
	ParamVal = ParamVals.Find(FString(TEXT("Section")));
	FString SectionName;

	if ( ParamVal )
	{
		SectionName = *ParamVal;
	}
	else
	{
		UE_LOG(LogGatherTextFromMetaDataCommandlet, Error, TEXT("No config section specified."));
		return -1;
	}

	//Modules to Preload
	TArray<FString> ModulesToPreload;
	GetStringArrayFromConfig(*SectionName, TEXT("ModulesToPreload"), ModulesToPreload, GatherTextConfigPath);

	for (const FString& ModuleName : ModulesToPreload)
	{
		FModuleManager::Get().LoadModule(*ModuleName);
	}

	// IncludePathFilters
	TArray<FString> IncludePathFilters;
	GetPathArrayFromConfig(*SectionName, TEXT("IncludePathFilters"), IncludePathFilters, GatherTextConfigPath);

	// IncludePaths (DEPRECATED)
	{
		TArray<FString> IncludePaths;
		GetPathArrayFromConfig(*SectionName, TEXT("IncludePaths"), IncludePaths, GatherTextConfigPath);
		if (IncludePaths.Num())
		{
			IncludePathFilters.Append(IncludePaths);
			UE_LOG(LogGatherTextFromMetaDataCommandlet, Warning, TEXT("IncludePaths detected in section %s. IncludePaths is deprecated, please use IncludePathFilters."), *SectionName);
		}
	}

	if (IncludePathFilters.Num() == 0)
	{
		UE_LOG(LogGatherTextFromMetaDataCommandlet, Error, TEXT("No include path filters in section %s."), *SectionName);
		return -1;
	}

	// ExcludePathFilters
	TArray<FString> ExcludePathFilters;
	GetPathArrayFromConfig(*SectionName, TEXT("ExcludePathFilters"), ExcludePathFilters, GatherTextConfigPath);

	// ExcludePaths (DEPRECATED)
	{
		TArray<FString> ExcludePaths;
		GetPathArrayFromConfig(*SectionName, TEXT("ExcludePaths"), ExcludePaths, GatherTextConfigPath);
		if (ExcludePaths.Num())
		{
			ExcludePathFilters.Append(ExcludePaths);
			UE_LOG(LogGatherTextFromMetaDataCommandlet, Warning, TEXT("ExcludePaths detected in section %s. ExcludePaths is deprecated, please use ExcludePathFilters."), *SectionName);
		}
	}

	FGatherParameters Arguments;
	GetStringArrayFromConfig(*SectionName, TEXT("InputKeys"), Arguments.InputKeys, GatherTextConfigPath);
	GetStringArrayFromConfig(*SectionName, TEXT("OutputNamespaces"), Arguments.OutputNamespaces, GatherTextConfigPath);
	TArray<FString> OutputKeys;
	GetStringArrayFromConfig(*SectionName, TEXT("OutputKeys"), OutputKeys, GatherTextConfigPath);
	for(const auto& OutputKey : OutputKeys)
	{
		Arguments.OutputKeys.Add(FText::FromString(OutputKey));
	}

	// Execute gather.
	GatherTextFromUObjects(IncludePathFilters, ExcludePathFilters, Arguments);

	// Add any manifest dependencies if they were provided
	TArray<FString> ManifestDependenciesList;
	GetPathArrayFromConfig(*SectionName, TEXT("ManifestDependencies"), ManifestDependenciesList, GatherTextConfigPath);


	if( !ManifestInfo->AddManifestDependencies( ManifestDependenciesList ) )
	{
		UE_LOG(LogGatherTextFromMetaDataCommandlet, Error, TEXT("The GatherTextFromMetaData commandlet couldn't find all the specified manifest dependencies."));
		return -1;
	}

	return 0;
}

void UGatherTextFromMetaDataCommandlet::GatherTextFromUObjects(const TArray<FString>& IncludePaths, const TArray<FString>& ExcludePaths, const FGatherParameters& Arguments)
{
	for(TObjectIterator<UField> It; It; ++It)
	{
		FString SourceFilePath;
		FSourceCodeNavigation::FindClassHeaderPath(*It, SourceFilePath);
		SourceFilePath = FPaths::ConvertRelativePathToFull(SourceFilePath);

		check(!SourceFilePath.IsEmpty());

		// Returns true if in an include path. False otherwise.
		auto IncludePathLogic = [&]() -> bool
		{
			for(int32 i = 0; i < IncludePaths.Num(); ++i)
			{
				if(SourceFilePath.MatchesWildcard(IncludePaths[i]))
				{
					return true;
				}
			}
			return false;
		};
		if(!IncludePathLogic())
		{
			continue;
		}

		// Returns true if in an exclude path. False otherwise.
		auto ExcludePathLogic = [&]() -> bool
		{
			for(int32 i = 0; i < ExcludePaths.Num(); ++i)
			{
				if(SourceFilePath.MatchesWildcard(ExcludePaths[i]))
				{
					return true;
				}
			}
			return false;
		};
		if(ExcludePathLogic())
		{
			continue;
		}

		GatherTextFromUObject(*It, Arguments);
	}
}

void UGatherTextFromMetaDataCommandlet::GatherTextFromUObject(UField* const Field, const FGatherParameters& Arguments)
{
	// Gather for object.
	{
		if( !Field->HasMetaData( TEXT("DisplayName") ) )
		{
			Field->SetMetaData( TEXT("DisplayName"), *FName::NameToDisplayString( Field->GetName(), Field->IsA( UBoolProperty::StaticClass() ) ) );
		}

		for(int32 i = 0; i < Arguments.InputKeys.Num(); ++i)
		{
			FFormatNamedArguments PatternArguments;
			PatternArguments.Add( TEXT("FieldPath"), FText::FromString( Field->GetFullGroupName(false) ) );

			if( Field->HasMetaData( *Arguments.InputKeys[i] ) )
			{
				const FString& MetaDataValue = Field->GetMetaData(*Arguments.InputKeys[i]);
				if( !MetaDataValue.IsEmpty() )
				{
					PatternArguments.Add( TEXT("MetaDataValue"), FText::FromString(MetaDataValue) );

					const FString Namespace = Arguments.OutputNamespaces[i];
					FLocItem LocItem(MetaDataValue);
					FContext Context;
					Context.Key = FText::Format(Arguments.OutputKeys[i], PatternArguments).ToString();
					Context.SourceLocation = FString::Printf(TEXT("From metadata for key %s of member %s in %s"), *Arguments.InputKeys[i], *Field->GetName(), *Field->GetFullGroupName(true));
					ManifestInfo->AddEntry(TEXT("EntryDescription"), Namespace, LocItem, Context);
				}
			}
		}
	}

	// For enums, also gather for enum values.
	{
		UEnum* Enum = Cast<UEnum>(Field);
		if(Enum)
		{
			const int32 ValueCount = Enum->NumEnums();
			for(int32 i = 0; i < ValueCount; ++i)
			{
				if( !Enum->HasMetaData(TEXT("DisplayName"), i) )
				{
					Enum->SetMetaData(TEXT("DisplayName"), *FName::NameToDisplayString(Enum->GetEnumName(i), false), i);
				}

				for(int32 j = 0; j < Arguments.InputKeys.Num(); ++j)
				{
					FFormatNamedArguments PatternArguments;
					PatternArguments.Add( TEXT("FieldPath"), FText::FromString( Enum->GetFullGroupName(false) + TEXT(".") + Enum->GetEnumName(i) ) );

					if( Enum->HasMetaData(*Arguments.InputKeys[j], i) )
					{
						const FString& MetaDataValue = Enum->GetMetaData(*Arguments.InputKeys[j], i);
						if( !MetaDataValue.IsEmpty() )
						{
							PatternArguments.Add( TEXT("MetaDataValue"), FText::FromString(MetaDataValue) );

							const FString Namespace = Arguments.OutputNamespaces[j];
							FLocItem LocItem(MetaDataValue);
							FContext Context;
							Context.Key = FText::Format(Arguments.OutputKeys[j], PatternArguments).ToString();
							Context.SourceLocation = FString::Printf(TEXT("From metadata for key %s of enum value %s of enum %s in %s"), *Arguments.InputKeys[j], *Enum->GetEnumName(i), *Enum->GetName(), *Enum->GetFullGroupName(true));
							ManifestInfo->AddEntry(TEXT("EntryDescription"), Namespace, LocItem, Context);
						}
					}
				}
			}
		}
	}
}