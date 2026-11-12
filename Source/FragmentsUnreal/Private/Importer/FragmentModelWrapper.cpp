// Copyright 2026 Jairo Andres Basto Picott and Arkoun Merchant




#include "Importer/FragmentModelWrapper.h"
#include "Fragment/Fragment.h"
#include "Importer/FragmentOverrides.h"
#include "Materials/MaterialInterface.h"
#include "Utils/FragmentsLog.h"
#include "Utils/FragmentsUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogFragmentModelWrapper, Log, All);

void UFragmentModelWrapper::BuildFragmentRegistry(const FString& ModelGuid)
{
	if (!ParsedModel)
	{
		UE_LOG(LogFragmentModelWrapper, Error, TEXT("Cannot build fragment registry - model not loaded"));
		return;
	}

	FragmentRegistry = NewObject<UFragmentRegistry>(this);
	FragmentRegistry->BuildFromModel(this, ModelGuid);

	UE_LOG(LogFragmentModelWrapper, Log, TEXT("Fragment registry built for model: %s (%d fragments)"),
	       *ModelGuid, FragmentRegistry->GetFragmentCount());
}

void UFragmentModelWrapper::ResetWrapper()
{
	SpawnedFragment = nullptr;
	MaterialsMap.Empty();
	MaterialOverrides.Empty();
	ResolvedOverrideMaterials.Empty();
	FragmentPath.Reset();

	// Clear cell partition state.
	ShellCells.Empty();
	InteriorCells.Empty();
	LocalIdToFloor.Empty();
	ModelAnchor = FVector::ZeroVector;
	bShellCellsFinalized = false;
	bShellCellsMergeEnqueued = false;

	LocalIdToFlatIndex.Empty();
	LocalIdToItem.Empty();
	ClearRelationIndex();
}

void UFragmentModelWrapper::ClearRelationIndex()
{
	LocalIdToRelationRows.Empty();
	bRelationIndexReady = false;
	AttributeQueryCache.Empty();
	LocalIdToStoreyName.Empty();
	bStoreyNamesReady = false;
}

const TArray<int64>* UFragmentModelWrapper::FindAttributeQuery(const FString& Key) const
{
	return AttributeQueryCache.Find(Key);
}

void UFragmentModelWrapper::RememberAttributeQuery(const FString& Key, TArray<int64> Ids)
{
	if (AttributeQueryCache.Num() >= 16)
	{
		AttributeQueryCache.Empty();
	}
	AttributeQueryCache.Add(Key, MoveTemp(Ids));
}

void UFragmentModelWrapper::SetStoreyNames(TMap<int64, FString> Names)
{
	LocalIdToStoreyName = MoveTemp(Names);
	bStoreyNamesReady = true;
}

void UFragmentModelWrapper::EnsureRelationIndex()
{
	if (bRelationIndexReady)
	{
		return;
	}
	bRelationIndexReady = true;
	if (!ParsedModel || !ParsedModel->relations() || !ParsedModel->relations_items())
	{
		return;
	}
	const auto* Relations = ParsedModel->relations();
	const auto* Owners = ParsedModel->relations_items();
	const flatbuffers::uoffset_t Count = FMath::Min(Relations->size(), Owners->size());
	for (flatbuffers::uoffset_t Row = 0; Row < Count; ++Row)
	{
		const int32 Owner = Owners->Get(Row);
		if (Owner < 0)
		{
			continue;
		}
		LocalIdToRelationRows.FindOrAdd(static_cast<int64>(Owner)).Add(static_cast<int32>(Row));
	}
}

const TArray<int32>* UFragmentModelWrapper::FindRelationRows(int64 LocalId) const
{
	return LocalIdToRelationRows.Find(LocalId);
}

int64 UFragmentModelWrapper::GetFlatIndexForLocalId(int64 LocalId)
{
	if (LocalIdToFlatIndex.IsEmpty() && ParsedModel && ParsedModel->local_ids())
	{
		const auto* LocalIds = ParsedModel->local_ids();
		LocalIdToFlatIndex.Reserve(LocalIds->size());
		for (flatbuffers::uoffset_t i = 0; i < LocalIds->size(); i++)
		{
			LocalIdToFlatIndex.Add(LocalIds->Get(i), static_cast<int32>(i));
		}
	}
	const int32* Found = LocalIdToFlatIndex.Find(LocalId);
	return Found ? static_cast<int64>(*Found) : INDEX_NONE;
}

FFragmentItem* UFragmentModelWrapper::FindItemByLocalId(int64 LocalId)
{
	if (LocalIdToItem.IsEmpty())
	{
		if (ParsedModel && ParsedModel->local_ids())
		{
			LocalIdToItem.Reserve(ParsedModel->local_ids()->size());
		}
		UFragmentsUtils::BuildLocalIdToItemMap(ModelItem, LocalIdToItem);
	}
	return LocalIdToItem.FindRef(LocalId);
}

bool UFragmentModelWrapper::ReferencesWorld(const UWorld* World) const
{
	if (!World) return false;

	if (const AFragment* Frag = SpawnedFragment)
	{
		if (Frag->GetWorld() == World)
		{
			return true;
		}
	}

	for (const TPair<int32, UMaterialInstanceDynamic*>& KV : MaterialsMap)
	{
		if (const UMaterialInstanceDynamic* MID = KV.Value)
		{
			if (const UObject* Outer = MID->GetOuter())
			{
				if (const UWorld* MIDWorld = Outer->GetWorld())
				{
					if (MIDWorld == World) return true;
				}
			}
		}
	}

	return false;
}

UMaterialInterface* UFragmentModelWrapper::ResolveOverrideMaterial(
	uint8 R, uint8 G, uint8 B, uint8 A, bool bIsGlass)
{
	const FString Key = UE::Fragments::Overrides::BuildSourceKey(R, G, B, A, bIsGlass);

	if (TObjectPtr<UMaterialInterface>* Cached = ResolvedOverrideMaterials.Find(Key))
	{
		return Cached->Get();
	}

	const FString* Path = MaterialOverrides.Find(Key);
	if (!Path || Path->IsEmpty())
	{
		ResolvedOverrideMaterials.Add(Key, nullptr);
		return nullptr;
	}

	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, **Path);
	if (!Mat)
	{
		UE_LOG(LogFragments, Warning,
			TEXT("fragmentOverrides: could not load replacement '%s' for source '%s'"),
			**Path, *Key);
	}
	ResolvedOverrideMaterials.Add(Key, Mat);
	return Mat;
}

bool UFragmentModelWrapper::IsValidModelBuffer(const TArray<uint8>& Buffer)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FragWrapper_VerifyModelBuffer);
	if (Buffer.Num() == 0) { return false; }

	// The default table cap (1M) is below what a large campus model holds, and would reject a valid file.
	flatbuffers::Verifier::Options Options;
	Options.max_tables = TNumericLimits<flatbuffers::uoffset_t>::Max();
	flatbuffers::Verifier Verifier(Buffer.GetData(), static_cast<size_t>(Buffer.Num()), Options);
	return Verifier.VerifyBuffer<Model>(nullptr);
}
