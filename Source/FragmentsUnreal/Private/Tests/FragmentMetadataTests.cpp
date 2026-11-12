// Copyright 2026 Jairo Andres Basto Picott and Arkoun Merchant
#include "Misc/AutomationTest.h"
#include "Importer/FragmentModelWrapper.h"
#include "Importer/FragmentsImporter.h"
#include "Interfaces/IPluginManager.h"
#include "Utils/FragmentMetadata.h"
#include "Utils/FragmentTupleParser.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFragmentTupleParserTest,
	"FragmentsUnreal.Metadata.TupleParser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFragmentTupleParserTest::RunTest(const FString& Parameters)
{
	using namespace UE::Fragments::Tuple;
	TArray<FString> Tokens;

	SplitTuple(TEXT("[\"Wall, Exterior\",\"Door\",1]"), Tokens);
	TestEqual(TEXT("comma inside quotes stays one value"), Tokens.Num(), 3);
	if (Tokens.Num() == 3)
	{
		TestEqual(TEXT("quoted comma value"), Tokens[0], FString(TEXT("Wall, Exterior")));
		TestEqual(TEXT("second token"), Tokens[1], FString(TEXT("Door")));
	}

	SplitTuple(TEXT("[\"a[b]\",\"c\",2]"), Tokens);
	TestEqual(TEXT("brackets inside quotes"), Tokens.Num() > 0 ? Tokens[0] : FString(), FString(TEXT("a[b]")));

	SplitTuple(TEXT("[\"Name\",[\"a\",\"b\"],3]"), Tokens);
	TestEqual(TEXT("nested array is one value"), Tokens.Num() > 1 ? Tokens[1] : FString(), FString(TEXT("[\"a\",\"b\"]")));

	SplitTuple(TEXT("[\"a\\\"b\",\"c\\\\d\",4]"), Tokens);
	TestEqual(TEXT("escaped quote"), Tokens.Num() > 0 ? Tokens[0] : FString(), FString(TEXT("a\"b")));
	TestEqual(TEXT("escaped slash"), Tokens.Num() > 1 ? Tokens[1] : FString(), FString(TEXT("c\\d")));

	SplitTuple(TEXT("[null,5]"), Tokens);
	TestEqual(TEXT("bare null is empty"), Tokens.Num() > 0 ? Tokens[0] : FString(TEXT("x")), FString());

	SplitTuple(TEXT("[\"\\u00e9\",6]"), Tokens);
	TestEqual(TEXT("unicode escape"), Tokens.Num() > 0 ? Tokens[0] : FString(), FString(TEXT("é")));

	SplitTuple(TEXT("[12.5]"), Tokens);
	TestEqual(TEXT("bare number"), Tokens.Num() > 0 ? Tokens[0] : FString(), FString(TEXT("12.5")));

	SplitTuple(TEXT("[\"\"]"), Tokens);
	TestEqual(TEXT("empty string"), Tokens.Num(), 1);
	if (Tokens.Num() == 1)
	{
		TestTrue(TEXT("empty string value"), Tokens[0].IsEmpty());
	}

	SplitTuple(TEXT("[\"unterminated"), Tokens);
	TestEqual(TEXT("unterminated string is kept"), Tokens.Num() > 0 ? Tokens[0] : FString(), FString(TEXT("unterminated")));

	FString Huge = TEXT("[");
	Huge.Reserve(MaxTupleBytes + 8);
	while (Huge.Len() <= MaxTupleBytes)
	{
		Huge.AppendChar(TEXT('1'));
	}
	Huge.AppendChar(TEXT(']'));
	SplitTuple(Huge, Tokens);
	TestEqual(TEXT("tuple over the size limit yields nothing"), Tokens.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFragmentPropertyWalkTest,
	"FragmentsUnreal.Metadata.PropertyWalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFragmentPropertyWalkTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("FragmentsUnreal"));
	const FString Path = Plugin.IsValid()
		? FPaths::Combine(Plugin->GetContentDir(), TEXT("Resources"), TEXT("small_test.frag"))
		: FString();
	TArray<uint8> Sample;
	FString Error;
	if (!TestTrue(TEXT("sample inflates"), UFragmentsImporter::ReadAndInflate(Path, Sample, Error)))
	{
		return false;
	}

	UFragmentModelWrapper* Wrapper = NewObject<UFragmentModelWrapper>(GetTransientPackage());
	Wrapper->AddToRoot();
	Wrapper->LoadModel(MoveTemp(Sample));

	const int64 WallId = 186;
	const TArray<FItemAttribute> Direct = [&]()
	{
		const Model* Parsed = Wrapper->GetParsedModel();
		const int64 Index = Wrapper->GetFlatIndexForLocalId(WallId);
		if (!Parsed || !Parsed->attributes() || Index == INDEX_NONE)
		{
			return TArray<FItemAttribute>();
		}
		return UFragmentsUtils::ParseItemAttribute(Parsed->attributes()->Get(static_cast<flatbuffers::uoffset_t>(Index)));
	}();
	const FItemAttribute* NameAttribute = Direct.FindByPredicate([](const FItemAttribute& Attribute)
	{
		return Attribute.Key.Equals(TEXT("Name"), ESearchCase::IgnoreCase);
	});
	TestTrue(TEXT("type code uses 64 bits"), NameAttribute && NameAttribute->TypeHash == 3258342251LL);

	const UE::Fragments::Metadata::FGatheredElement Wall = UE::Fragments::Metadata::Gather(Wrapper, WallId);
	auto FindSet = [&Wall](const TCHAR* SetName) -> const FFragmentPropertySetGroup*
	{
		return Wall.Sets.FindByPredicate([SetName](const FFragmentPropertySetGroup& Set)
		{
			return Set.Name.Equals(SetName, ESearchCase::IgnoreCase);
		});
	};

	const FFragmentPropertySetGroup* TakeOff = FindSet(TEXT("Pset_QuantityTakeOff"));
	TestTrue(TEXT("quantity takeoff set"), TakeOff != nullptr);
	if (TakeOff)
	{
		const FItemAttribute* Reference = TakeOff->Rows.FindByPredicate([](const FItemAttribute& Row)
		{
			return Row.Key.Equals(TEXT("Reference"), ESearchCase::IgnoreCase);
		});
		TestTrue(TEXT("reference value"), Reference && Reference->Value.Contains(TEXT("yeso")));
	}
	TestNotNull(TEXT("wall common set"), FindSet(TEXT("Pset_WallCommon")));
	TestNotNull(TEXT("reinforcement set"), FindSet(TEXT("Pset_ReinforcementBarPitchOfWall")));

	bool bSawLayerThickness = false;
	for (const FFragmentPropertySetGroup& Set : Wall.Sets)
	{
		for (const FItemAttribute& Row : Set.Rows)
		{
			if (Row.Key.Equals(TEXT("LayerThickness"), ESearchCase::IgnoreCase))
			{
				bSawLayerThickness = true;
			}
		}
	}
	TestFalse(TEXT("material layers are not property rows"), bSawLayerThickness);

	const FFragmentMaterialInfo* Brick = Wall.Materials.FindByPredicate([](const FFragmentMaterialInfo& Material)
	{
		return Material.Name.Contains(TEXT("Ladrillo"));
	});
	TestTrue(TEXT("brick layer"), Brick && FMath::IsNearlyEqual(Brick->Thickness, 0.07f, 0.001f));
	if (Brick)
	{
		TestTrue(TEXT("layer set name"), Brick->LayerSetName.Contains(TEXT("Partición")));
	}
	TestEqual(TEXT("three layers"), Wall.Materials.Num(), 3);

	Wrapper->RemoveFromRoot();
	return true;
}

#endif
