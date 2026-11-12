#include "Common/SettingsOverride.h"
#include "UObject/UnrealType.h"

namespace
{
	bool ParseNumber(const FString& Text, double& Out)
	{
		const FString T = Text.TrimStartAndEnd();
		if (T.IsEmpty())
		{
			return false;
		}
		if (!FCString::IsNumeric(*T))
		{
			return false;
		}
		Out = FCString::Atod(*T);
		return true;
	}

	bool SetLeaf(FProperty* Prop, void* ValuePtr, const FString& RawValue, FString& OutError)
	{
		const FString Value = RawValue.TrimStartAndEnd();
		if (FBoolProperty* Bool = CastField<FBoolProperty>(Prop))
		{
			if (Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1")) { Bool->SetPropertyValue(ValuePtr, true); return true; }
			if (Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Value == TEXT("0")) { Bool->SetPropertyValue(ValuePtr, false); return true; }
			OutError = FString::Printf(TEXT("'%s' is not a bool"), *Value);
			return false;
		}
		if (FNumericProperty* Number = CastField<FNumericProperty>(Prop))
		{
			double D = 0.0;
			if (!ParseNumber(Value, D))
			{
				OutError = FString::Printf(TEXT("'%s' is not a number"), *Value);
				return false;
			}
			if (Number->IsFloatingPoint())
			{
				Number->SetFloatingPointPropertyValue(ValuePtr, D);
				return true;
			}
			if (FMath::Frac(D) != 0.0)
			{
				OutError = FString::Printf(TEXT("'%s' is not an integer"), *Value);
				return false;
			}
			Number->SetIntPropertyValue(ValuePtr, static_cast<int64>(D));
			return true;
		}
		if (!Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None))
		{
			OutError = FString::Printf(TEXT("cannot parse '%s'"), *Value);
			return false;
		}
		return true;
	}
}

namespace FPSSettingsOverride
{
	bool Apply(UObject& Target, const FString& Spec, TArray<FString>& OutApplied, FString& OutError)
	{
		TArray<FString> Items;
		Spec.ParseIntoArray(Items, TEXT(";"), /*InCullEmpty=*/true);
		for (const FString& Item : Items)
		{
			FString Path;
			FString Value;
			if (!Item.Split(TEXT("="), &Path, &Value))
			{
				OutError = FString::Printf(TEXT("'%s' is not Path=Value"), *Item.TrimStartAndEnd());
				return false;
			}
			Path.TrimStartAndEndInline();
			TArray<FString> Parts;
			Path.ParseIntoArray(Parts, TEXT("."), /*InCullEmpty=*/true);
			if (Parts.Num() == 0)
			{
				OutError = FString::Printf(TEXT("'%s' has an empty path"), *Item.TrimStartAndEnd());
				return false;
			}
			const UStruct* Struct = Target.GetClass();
			void* Container = &Target;
			for (int32 i = 0; i < Parts.Num(); ++i)
			{
				FProperty* Prop = FindFProperty<FProperty>(Struct, FName(*Parts[i]));
				if (!Prop)
				{
					OutError = FString::Printf(TEXT("unknown setting '%s' (no '%s')"), *Path, *Parts[i]);
					return false;
				}
				void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container);
				if (i + 1 < Parts.Num())
				{
					const FStructProperty* StructProp = CastField<FStructProperty>(Prop);
					if (!StructProp)
					{
						OutError = FString::Printf(TEXT("'%s' in '%s' is not a struct"), *Parts[i], *Path);
						return false;
					}
					Struct = StructProp->Struct;
					Container = ValuePtr;
				}
				else if (!SetLeaf(Prop, ValuePtr, Value, OutError))
				{
					OutError = FString::Printf(TEXT("%s: %s"), *Path, *OutError);
					return false;
				}
			}
			OutApplied.Add(FString::Printf(TEXT("%s=%s"), *Path, *Value.TrimStartAndEnd()));
		}
		return true;
	}
}
