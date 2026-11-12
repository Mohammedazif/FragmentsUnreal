// Copyright 2026 Jairo Andres Basto Picott and Arkoun Merchant

#pragma once

#include "CoreMinimal.h"
#include "Utils/FragmentsUtils.h"

class UFragmentModelWrapper;

namespace UE::Fragments::Metadata
{
	/** Property sets, materials, classifications and type for one local id. Read from the FlatBuffer on demand. */
	struct FGatheredElement
	{
		TArray<FFragmentPropertySetGroup> Sets;
		TArray<FFragmentMaterialInfo> Materials;
		TArray<FFragmentClassificationInfo> Classifications;
		FString TypeName;
		int64 TypeLocalId = INDEX_NONE;
	};

	FGatheredElement Gather(UFragmentModelWrapper* Wrapper, int64 LocalId);

	TArray<int64> FindItemsByAttribute(UFragmentModelWrapper* Wrapper, const FString& AttributeName, const FString& AttributeValue, bool bExactMatch);
	TArray<int64> FindItemsByStorey(UFragmentModelWrapper* Wrapper, const FString& StoreyName);
	TMap<FString, int32> GetCategoryCounts(UFragmentModelWrapper* Wrapper);
	TMap<FString, int32> GetStoreyCounts(UFragmentModelWrapper* Wrapper);
}
