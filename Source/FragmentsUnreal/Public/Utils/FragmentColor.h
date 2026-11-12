// Copyright 2026 Jairo Andres Basto Picott and Arkoun Merchant

// Fragments material bytes are sRGB. BaseColor parameters are linear.

#pragma once

#include "CoreMinimal.h"

namespace UE::Fragments::Color
{
	/** Linear BaseColor. R, G and B use the sRGB curve. Alpha stays A/255. */
	inline FLinearColor BaseColorFromSrgbBytes(uint8 R, uint8 G, uint8 B, uint8 A)
	{
		return FLinearColor::FromSRGBColor(FColor(R, G, B, A));
	}
}
