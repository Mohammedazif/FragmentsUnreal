// Copyright 2026 Jairo Andres Basto Picott and Arkoun Merchant

#include "Utils/FragmentMetadata.h"
#include "Importer/FragmentModelWrapper.h"
#include "Utils/FragmentTupleParser.h"

namespace
{
	constexpr int32 MaxPropertyNameChars = 256;
	constexpr int32 MaxPropertyValueChars = 4096;
	constexpr int32 MaxItemAttributes = 256;
	constexpr int32 MaxItemRelations = 256;
	constexpr int32 MaxRelationTargets = 65536;
	constexpr int32 MaxPropertyUnitsPerItem = 8192;
	constexpr int32 MaxAssociationNodesPerItem = 512;
	constexpr int32 MaxAssociationTextChars = 256;
	constexpr int32 MaxTypeAttributes = 256;

	const FString RelIsDefinedBy(TEXT("IsDefinedBy"));
	const FString RelHasPropertySets(TEXT("HasPropertySets"));
	const FString RelHasProperties(TEXT("HasProperties"));
	const FString RelQuantities(TEXT("Quantities"));
	const FString RelHasAssociations(TEXT("HasAssociations"));
	const FString RelMaterials(TEXT("Materials"));
	const FString RelForLayerSet(TEXT("ForLayerSet"));
	const FString RelMaterialLayers(TEXT("MaterialLayers"));
	const FString RelMaterial(TEXT("Material"));
	const FString RelObjectTypeOf(TEXT("ObjectTypeOf"));

	struct FRelation
	{
		FString Name;
		TArray<int64> Targets;
	};

	FString CategoryOf(UFragmentModelWrapper* Wrapper, int64 LocalId)
	{
		const Model* Parsed = Wrapper ? Wrapper->GetParsedModel() : nullptr;
		const int64 Index = Wrapper ? Wrapper->GetFlatIndexForLocalId(LocalId) : INDEX_NONE;
		if (!Parsed || !Parsed->categories() || Index == INDEX_NONE || Index >= Parsed->categories()->size())
		{
			return FString();
		}
		const auto* Category = Parsed->categories()->Get(static_cast<flatbuffers::uoffset_t>(Index));
		return Category ? FString(UTF8_TO_TCHAR(Category->c_str())) : FString();
	}

	TArray<FItemAttribute> AttributesOf(UFragmentModelWrapper* Wrapper, int64 LocalId)
	{
		TArray<FItemAttribute> ParsedAttributes;
		const Model* Parsed = Wrapper ? Wrapper->GetParsedModel() : nullptr;
		const int64 Index = Wrapper ? Wrapper->GetFlatIndexForLocalId(LocalId) : INDEX_NONE;
		if (!Parsed || !Parsed->attributes() || Index == INDEX_NONE || Index >= Parsed->attributes()->size())
		{
			return ParsedAttributes;
		}
		return UFragmentsUtils::ParseItemAttribute(Parsed->attributes()->Get(static_cast<flatbuffers::uoffset_t>(Index)));
	}

	FString ItemName(const TArray<FItemAttribute>& Attributes, const FString& Category)
	{
		for (const FItemAttribute& Attribute : Attributes)
		{
			if (Attribute.Key.Equals(TEXT("Name"), ESearchCase::IgnoreCase) && !Attribute.Value.IsEmpty())
			{
				return Attribute.Value.Left(MaxPropertyNameChars);
			}
		}
		return Category.Left(MaxPropertyNameChars);
	}

	FString AttributeValue(const TArray<FItemAttribute>& Attributes, const TCHAR* Key)
	{
		for (const FItemAttribute& Attribute : Attributes)
		{
			if (Attribute.Key.Equals(Key, ESearchCase::IgnoreCase))
			{
				return Attribute.Value;
			}
		}
		return FString();
	}

	TArray<FRelation> RelationsOf(UFragmentModelWrapper* Wrapper, int64 LocalId)
	{
		TArray<FRelation> Relations;
		Wrapper->EnsureRelationIndex();
		const TArray<int32>* Rows = Wrapper->FindRelationRows(LocalId);
		const Model* Parsed = Wrapper->GetParsedModel();
		if (!Rows || !Parsed || !Parsed->relations())
		{
			return Relations;
		}

		const auto* RelationTable = Parsed->relations();
		for (int32 Row : *Rows)
		{
			if (Relations.Num() >= MaxItemRelations || Row < 0 || static_cast<flatbuffers::uoffset_t>(Row) >= RelationTable->size())
			{
				break;
			}
			const auto* Entry = RelationTable->Get(static_cast<flatbuffers::uoffset_t>(Row));
			if (!Entry || !Entry->data())
			{
				continue;
			}
			const auto* Data = Entry->data();
			const uint32 Count = FMath::Min(Data->size(), static_cast<uint32>(MaxItemRelations - Relations.Num()));
			for (uint32 TupleIndex = 0; TupleIndex < Count; ++TupleIndex)
			{
				const auto* Raw = Data->Get(TupleIndex);
				if (!Raw || Raw->size() > static_cast<uint32>(UE::Fragments::Tuple::MaxTupleBytes))
				{
					continue;
				}
				TArray<FString> Tokens;
				UE::Fragments::Tuple::SplitTuple(FString(UTF8_TO_TCHAR(Raw->c_str())), Tokens);
				if (Tokens.Num() == 0)
				{
					continue;
				}
				FRelation Relation;
				Relation.Name = Tokens[0].Left(MaxPropertyNameChars);
				const int32 TargetCount = FMath::Min(Tokens.Num() - 1, MaxRelationTargets);
				for (int32 TargetIndex = 1; TargetIndex <= TargetCount; ++TargetIndex)
				{
					const int64 TargetId = FCString::Atoi64(*Tokens[TargetIndex]);
					if (TargetId > 0 && TargetId <= MAX_uint32)
					{
						Relation.Targets.Add(TargetId);
					}
				}
				Relations.Add(MoveTemp(Relation));
			}
		}
		return Relations;
	}

	const FRelation* FindRelation(const TArray<FRelation>& Relations, const FString& Name)
	{
		for (const FRelation& Relation : Relations)
		{
			if (Relation.Name == Name)
			{
				return &Relation;
			}
		}
		return nullptr;
	}

	bool ReadProperty(const TArray<FItemAttribute>& Attributes, const FString& Category, FItemAttribute& OutRow)
	{
		OutRow = FItemAttribute();
		OutRow.Key = ItemName(Attributes, Category);
		if (OutRow.Key.IsEmpty())
		{
			return false;
		}
		for (const FItemAttribute& Attribute : Attributes)
		{
			if (Attribute.Key.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			OutRow.Value = Attribute.Value.Left(MaxPropertyValueChars);
			OutRow.TypeHash = Attribute.TypeHash;
			break;
		}
		return true;
	}
}

FGatheredElement UE::Fragments::Metadata::Gather(UFragmentModelWrapper* Wrapper, int64 LocalId)
{
	FGatheredElement Result;
	if (!Wrapper || !Wrapper->GetParsedModel() || LocalId < 0)
	{
		return Result;
	}
	Wrapper->EnsureRelationIndex();

	TFunction<void(int64, int32, bool, TSet<int64>&, int32&)> GatherSets;
	GatherSets = [&](int64 TargetId, int32 Depth, bool bFromType, TSet<int64>& Visited, int32& Budget)
	{
		if (Depth > 2 || Budget <= 0 || Visited.Contains(TargetId))
		{
			return;
		}
		Visited.Add(TargetId);
		--Budget;

		const TArray<FRelation> Relations = RelationsOf(Wrapper, TargetId);
		const TArray<FItemAttribute> Attributes = AttributesOf(Wrapper, TargetId);
		const FString Category = CategoryOf(Wrapper, TargetId);
		bool bIsPropertySet = false;
		TSet<int64> SeenProperties;

		for (const FRelation& Relation : Relations)
		{
			if (--Budget <= 0)
			{
				break;
			}
			if (Relation.Name != RelHasProperties && Relation.Name != RelQuantities)
			{
				continue;
			}
			bIsPropertySet = true;

			FFragmentPropertySetGroup Set;
			Set.Name = ItemName(Attributes, Category);
			Set.LocalId = TargetId;
			Set.bFromType = bFromType;
			Set.bIsQuantitySet = Relation.Name == RelQuantities;

			for (int64 PropertyId : Relation.Targets)
			{
				if (--Budget <= 0)
				{
					break;
				}
				bool bAlreadySeen = false;
				SeenProperties.Add(PropertyId, &bAlreadySeen);
				if (bAlreadySeen)
				{
					continue;
				}
				FItemAttribute Row;
				if (!ReadProperty(AttributesOf(Wrapper, PropertyId), CategoryOf(Wrapper, PropertyId), Row))
				{
					continue;
				}
				Row.PropertySet = Set.Name;
				Row.bFromType = bFromType;
				Set.Rows.Add(MoveTemp(Row));
			}
			if (Set.Rows.Num() > 0)
			{
				Result.Sets.Add(MoveTemp(Set));
			}
		}

		if (bIsPropertySet)
		{
			return;
		}

		for (const FRelation& Relation : Relations)
		{
			if (--Budget <= 0)
			{
				break;
			}
			if (Relation.Name != RelHasPropertySets)
			{
				continue;
			}
			for (int64 ChildId : Relation.Targets)
			{
				if (--Budget <= 0)
				{
					break;
				}
				GatherSets(ChildId, Depth + 1, true, Visited, Budget);
			}
		}
	};

	TSet<int64> Visited;
	Visited.Add(LocalId);
	int32 Budget = MaxPropertyUnitsPerItem;
	const TArray<FRelation> OwnRelations = RelationsOf(Wrapper, LocalId);
	for (const FRelation& Relation : OwnRelations)
	{
		if (Relation.Name != RelIsDefinedBy && Relation.Name != RelHasPropertySets)
		{
			continue;
		}
		const bool bFromType = Relation.Name == RelHasPropertySets;
		for (int64 TargetId : Relation.Targets)
		{
			if (--Budget <= 0)
			{
				break;
			}
			GatherSets(TargetId, 1, bFromType, Visited, Budget);
		}
	}

	for (const FRelation& Relation : OwnRelations)
	{
		if (Relation.Name != RelIsDefinedBy || Result.TypeLocalId != INDEX_NONE)
		{
			continue;
		}
		for (int64 TargetId : Relation.Targets)
		{
			const FString Category = CategoryOf(Wrapper, TargetId);
			const TArray<FRelation> TargetRelations = RelationsOf(Wrapper, TargetId);
			if (!Category.EndsWith(TEXT("TYPE"), ESearchCase::IgnoreCase) && !FindRelation(TargetRelations, RelObjectTypeOf))
			{
				continue;
			}
			const TArray<FItemAttribute> TypeAttributes = AttributesOf(Wrapper, TargetId);
			Result.TypeName = ItemName(TypeAttributes, Category);
			Result.TypeLocalId = TargetId;
			if (TypeAttributes.Num() > 0)
			{
				FFragmentPropertySetGroup TypeSet;
				TypeSet.Name = Category.Left(MaxPropertyNameChars);
				TypeSet.LocalId = TargetId;
				TypeSet.bFromType = true;
				const int32 Count = FMath::Min(TypeAttributes.Num(), MaxTypeAttributes);
				for (int32 AttributeIndex = 0; AttributeIndex < Count; ++AttributeIndex)
				{
					FItemAttribute Row = TypeAttributes[AttributeIndex];
					Row.Key = Row.Key.Left(MaxPropertyNameChars);
					Row.Value = Row.Value.Left(MaxPropertyValueChars);
					Row.PropertySet = TypeSet.Name;
					Row.bFromType = true;
					TypeSet.Rows.Add(MoveTemp(Row));
				}
				Result.Sets.Add(MoveTemp(TypeSet));
			}
			break;
		}
	}

	TFunction<void(int64, const FString&, int32, TSet<int64>&, int32&)> AddAssociation;
	AddAssociation = [&](int64 TargetId, const FString& LayerSetName, int32 Depth, TSet<int64>& Seen, int32& AssociationBudget)
	{
		if (Depth > 4 || AssociationBudget <= 0 || Seen.Contains(TargetId))
		{
			return;
		}
		Seen.Add(TargetId);
		--AssociationBudget;

		const FString Category = CategoryOf(Wrapper, TargetId);
		const TArray<FItemAttribute> Attributes = AttributesOf(Wrapper, TargetId);
		const TArray<FRelation> Relations = RelationsOf(Wrapper, TargetId);
		const FString Name = ItemName(Attributes, FString());

		if (Category.Equals(TEXT("IFCMATERIAL"), ESearchCase::IgnoreCase))
		{
			if (!Name.IsEmpty())
			{
				FFragmentMaterialInfo Material;
				Material.Name = Name.Left(MaxAssociationTextChars);
				Material.LayerSetName = LayerSetName;
				Material.LocalId = TargetId;
				Result.Materials.Add(MoveTemp(Material));
			}
			return;
		}

		if (Category.Contains(TEXT("CLASSIFICATION"), ESearchCase::IgnoreCase))
		{
			FFragmentClassificationInfo Classification;
			Classification.Identification = AttributeValue(Attributes, TEXT("Identification"));
			if (Classification.Identification.IsEmpty())
			{
				Classification.Identification = AttributeValue(Attributes, TEXT("ItemReference"));
			}
			if (Classification.Identification.IsEmpty())
			{
				Classification.Identification = Category;
			}
			Classification.Identification = Classification.Identification.Left(MaxAssociationTextChars);
			Classification.Name = Name.Left(MaxAssociationTextChars);
			Classification.Category = Category.Left(MaxAssociationTextChars);
			Result.Classifications.Add(MoveTemp(Classification));
			return;
		}

		if (const FRelation* ListRelation = FindRelation(Relations, RelMaterials))
		{
			for (int64 ChildId : ListRelation->Targets)
			{
				if (--AssociationBudget <= 0)
				{
					break;
				}
				AddAssociation(ChildId, LayerSetName, Depth + 1, Seen, AssociationBudget);
			}
		}
		if (const FRelation* UsageRelation = FindRelation(Relations, RelForLayerSet))
		{
			for (int64 ChildId : UsageRelation->Targets)
			{
				if (--AssociationBudget <= 0)
				{
					break;
				}
				AddAssociation(ChildId, LayerSetName, Depth + 1, Seen, AssociationBudget);
			}
		}
		if (const FRelation* LayersRelation = FindRelation(Relations, RelMaterialLayers))
		{
			FString SetName = AttributeValue(Attributes, TEXT("LayerSetName")).Left(MaxAssociationTextChars);
			if (SetName.IsEmpty())
			{
				SetName = LayerSetName;
			}
			for (int64 LayerId : LayersRelation->Targets)
			{
				if (--AssociationBudget <= 0)
				{
					break;
				}
				const float Thickness = FCString::Atof(*AttributeValue(AttributesOf(Wrapper, LayerId), TEXT("LayerThickness")));
				const FRelation* MaterialRelation = FindRelation(RelationsOf(Wrapper, LayerId), RelMaterial);
				if (!MaterialRelation)
				{
					continue;
				}
				for (int64 MaterialId : MaterialRelation->Targets)
				{
					if (--AssociationBudget <= 0)
					{
						break;
					}
					const FString MaterialName = ItemName(AttributesOf(Wrapper, MaterialId), FString()).Left(MaxAssociationTextChars);
					if (MaterialName.IsEmpty())
					{
						continue;
					}
					FFragmentMaterialInfo Material;
					Material.Name = MaterialName;
					Material.LayerSetName = SetName;
					Material.Thickness = Thickness;
					Material.LocalId = MaterialId;
					Result.Materials.Add(MoveTemp(Material));
				}
			}
		}
	};

	TSet<int64> AssociationSeen;
	AssociationSeen.Add(LocalId);
	int32 AssociationBudget = MaxAssociationNodesPerItem;
	for (const FRelation& Relation : OwnRelations)
	{
		if (Relation.Name != RelHasAssociations)
		{
			continue;
		}
		for (int64 TargetId : Relation.Targets)
		{
			if (--AssociationBudget <= 0)
			{
				break;
			}
			AddAssociation(TargetId, FString(), 1, AssociationSeen, AssociationBudget);
		}
	}

	return Result;
}
