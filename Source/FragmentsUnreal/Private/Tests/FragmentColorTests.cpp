// Copyright 2026 Jairo Andres Basto Picott and Arkoun Merchant
#include "Misc/AutomationTest.h"
#include "Utils/FragmentColor.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFragmentColorSourceToLinearTest,
	"FragmentsUnreal.Color.SourceToLinear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFragmentColorSourceToLinearTest::RunTest(const FString& Parameters)
{
	using namespace UE::Fragments::Color;

	const FLinearColor Black = BaseColorFromSrgbBytes(0, 0, 0, 0);
	TestTrue(TEXT("0 stays 0"), Black.R == 0.f && Black.G == 0.f && Black.B == 0.f && Black.A == 0.f);

	const FLinearColor White = BaseColorFromSrgbBytes(255, 255, 255, 255);
	TestTrue(TEXT("255 stays 1"), White.R == 1.f && White.G == 1.f && White.B == 1.f && White.A == 1.f);

	const FLinearColor Mid = BaseColorFromSrgbBytes(128, 128, 128, 128);
	TestTrue(TEXT("128 sRGB is about 0.216"), FMath::IsNearlyEqual(Mid.R, 0.21586f, 0.002f));
	TestTrue(TEXT("rgb channels match"), Mid.R == Mid.G && Mid.G == Mid.B);
	TestTrue(TEXT("alpha 128 is about 0.502"), FMath::IsNearlyEqual(Mid.A, 128.f / 255.f, 0.001f));
	TestTrue(TEXT("alpha is not run through sRGB"), !FMath::IsNearlyEqual(Mid.A, Mid.R, 0.01f));

	const FColor Source(128, 64, 32, 200);
	TestTrue(TEXT("matches FromSRGBColor"), BaseColorFromSrgbBytes(128, 64, 32, 200).Equals(FLinearColor::FromSRGBColor(Source)));

	return true;
}

#endif
