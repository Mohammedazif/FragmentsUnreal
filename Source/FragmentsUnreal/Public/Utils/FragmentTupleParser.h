// Copyright 2026 Jairo Andres Basto Picott and Arkoun Merchant

// Splits one Fragments attribute or relation tuple. Adapted from FragmentsUE.

#pragma once

#include "CoreMinimal.h"

namespace UE::Fragments::Tuple
{
	inline constexpr int32 MaxTupleBytes = 4 * 1024 * 1024;
	inline constexpr int32 MaxTupleTokens = 1024 * 1024;

	inline void SplitTuple(const FString& In, TArray<FString>& Out)
	{
		Out.Reset();
		if (In.Len() > MaxTupleBytes)
		{
			return;
		}

		const int32 Len = In.Len();
		int32 Index = 0;
		while (Index < Len && In[Index] != TEXT('['))
		{
			++Index;
		}
		if (Index >= Len)
		{
			return;
		}
		++Index;

		while (Index < Len)
		{
			if (Out.Num() >= MaxTupleTokens)
			{
				break;
			}

			while (Index < Len && (FChar::IsWhitespace(In[Index]) || In[Index] == TEXT(',')))
			{
				++Index;
			}
			if (Index >= Len || In[Index] == TEXT(']'))
			{
				break;
			}

			if (In[Index] == TEXT('"'))
			{
				++Index;
				FString Token;
				while (Index < Len)
				{
					const TCHAR Char = In[Index];
					if (Char == TEXT('\\') && Index + 1 < Len)
					{
						const TCHAR Escaped = In[Index + 1];
						switch (Escaped)
						{
						case TEXT('n'): Token.AppendChar(TEXT('\n')); break;
						case TEXT('t'): Token.AppendChar(TEXT('\t')); break;
						case TEXT('r'): Token.AppendChar(TEXT('\r')); break;
						case TEXT('b'): Token.AppendChar(TEXT('\b')); break;
						case TEXT('f'): Token.AppendChar(TEXT('\f')); break;
						case TEXT('u'):
							if (Index + 5 < Len)
							{
								const FString Hex = In.Mid(Index + 2, 4);
								Token.AppendChar(static_cast<TCHAR>(FCString::Strtoi(*Hex, nullptr, 16)));
								Index += 4;
							}
							break;
						default:
							Token.AppendChar(Escaped);
							break;
						}
						Index += 2;
						continue;
					}
					if (Char == TEXT('"'))
					{
						++Index;
						break;
					}
					Token.AppendChar(Char);
					++Index;
				}
				Out.Add(MoveTemp(Token));
			}
			else
			{
				const int32 Start = Index;
				int32 Depth = 0;
				bool bInString = false;
				while (Index < Len)
				{
					const TCHAR Char = In[Index];
					if (bInString)
					{
						if (Char == TEXT('\\'))
						{
							Index += 2;
							continue;
						}
						if (Char == TEXT('"'))
						{
							bInString = false;
						}
					}
					else if (Char == TEXT('"'))
					{
						bInString = true;
					}
					else if (Char == TEXT('[') || Char == TEXT('{'))
					{
						++Depth;
					}
					else if (Char == TEXT('}'))
					{
						--Depth;
					}
					else if (Char == TEXT(']'))
					{
						if (Depth == 0)
						{
							break;
						}
						--Depth;
					}
					else if (Char == TEXT(',') && Depth == 0)
					{
						break;
					}
					++Index;
				}

				FString Token = In.Mid(Start, Index - Start).TrimStartAndEnd();
				if (Token.Equals(TEXT("null"), ESearchCase::IgnoreCase))
				{
					Token.Empty();
				}
				Out.Add(MoveTemp(Token));
			}
		}
	}
}
