// NetBunchSizeCheck.cpp

#include "NetBunchSizeCheck.h"

#include "HAL/IConsoleManager.h"
#include "UObject/Class.h"
#include "UObject/CoreNet.h"        // FNetBitWriter, UPackageMap
#include "UObject/SoftObjectPtr.h"
#include "UObject/UnrealType.h"     // FProperty 各種, FScriptArrayHelper, TFieldIterator

namespace
{
	// CVar が見つからない場合のフォールバック(UE5.3 の既定値)
	constexpr int32 FallbackMaxBunchBytes     = 64 * 1024; // net.MaxConstructedPartialBunchSizeBytes
	constexpr int32 FallbackMaxRepArraySize   = 2 * 1024;  // net.MaxRepArraySize
	constexpr int32 FallbackMaxRepArrayMemory = 65535;     // net.MaxRepArrayMemory

	// PackageMap なしで UObject 参照を数えるときの 1 個あたりの見積もり。
	// 実際は NetGUID の可変長整数(多くは 1〜3 バイト)なので、安全側に 4 バイトで数える。
	constexpr int64 ObjectRefEstimateBits = 32;

	int32 ReadCVarInt(const TCHAR* Name, int32 Fallback)
	{
		const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		return CVar ? CVar->GetInt() : Fallback;
	}

	/**
	 * 計測用ライター。
	 * FNetBitWriter は UObject* を PackageMap 経由で書くため、PackageMap が nullptr だと落ちる。
	 * 接続が無くても計測できるよう、PackageMap が無いときだけ FName と UObject* の書き方を差し替える。
	 */
	class FMeasureWriter final : public FNetBitWriter
	{
	public:
		explicit FMeasureWriter(UPackageMap* InPackageMap)
			: FNetBitWriter(InPackageMap, 0) // MaxBits = 0 : 必要に応じて自動拡張
		{
		}

		using FNetBitWriter::operator<<;

		virtual FArchive& operator<<(FName& Name) override
		{
			if (PackageMap != nullptr)
			{
				return FNetBitWriter::operator<<(Name);
			}

			// UPackageMap::StaticSerializeName の文字列送信と同じ並び
			// (ハードコード名フラグ 1bit + 名前文字列 + 番号)。
			// NAME_None などのハードコード名は実際はもっと小さいので、安全側の見積もりになる。
			uint8 bHardcoded = 0;
			SerializeBits(&bHardcoded, 1);

			FArchive& Ar = *this;
			FString PlainName = Name.GetPlainNameString();
			int32 Number = Name.GetNumber();
			Ar << PlainName;
			Ar << Number;
			return *this;
		}

		virtual FArchive& operator<<(UObject*& Object) override
		{
			if (PackageMap != nullptr)
			{
				return FNetBitWriter::operator<<(Object);
			}
			AddObjectRefEstimate();
			return *this;
		}

		void AddObjectRefEstimate()
		{
			++EstimatedObjectRefs;
			EstimatedExtraBits += ObjectRefEstimateBits;
		}

		int32 EstimatedObjectRefs = 0;
		int64 EstimatedExtraBits = 0;
	};

	struct FMeasureContext
	{
		FMeasureWriter& Writer;
		UPackageMap* PackageMap = nullptr;
		int32 MaxRepArraySize = FallbackMaxRepArraySize;
		int32 MaxRepArrayMemory = FallbackMaxRepArrayMemory;
		bool bArrayLimitExceeded = false;
		FString ArrayLimitDetail;
	};

	FMeasureContext MakeContext(FMeasureWriter& Writer, UPackageMap* PackageMap)
	{
		return FMeasureContext{
			Writer,
			PackageMap,
			ReadCVarInt(TEXT("net.MaxRepArraySize"), FallbackMaxRepArraySize),
			ReadCVarInt(TEXT("net.MaxRepArrayMemory"), FallbackMaxRepArrayMemory)
		};
	}

	/**
	 * FRepLayout と同じ配列チェック。超えているとエンジン側ではシリアライズ自体がエラーになり送られない。
	 * メモリ側は「要素数 × メモリ上の要素サイズ(sizeof)」で判定され、FString の中身の長さは関係ない。
	 */
	void CheckArrayLimits(FMeasureContext& Ctx, const FString& Name, int32 Num, int32 ElementSize)
	{
		const int64 MemoryBytes = int64(Num) * ElementSize;
		if (Num > Ctx.MaxRepArraySize || MemoryBytes > Ctx.MaxRepArrayMemory)
		{
			Ctx.bArrayLimitExceeded = true;
			Ctx.ArrayLimitDetail += FString::Printf(
				TEXT("%s: Num=%d (net.MaxRepArraySize=%d), Num*ElementSize=%d*%d=%lld (net.MaxRepArrayMemory=%d)\n"),
				*Name, Num, Ctx.MaxRepArraySize, Num, ElementSize, MemoryBytes, Ctx.MaxRepArrayMemory);
		}
	}

	/** 動的配列の要素数は uint16 で送られる */
	void WriteArrayNum(FMeasureContext& Ctx, int32 Num)
	{
		FArchive& Ar = Ctx.Writer;
		uint16 Num16 = static_cast<uint16>(FMath::Min(Num, 65535));
		Ar << Num16;
	}

	void SerializeValue(FMeasureContext& Ctx, const FProperty* Prop, void* ValuePtr);

	/** NetSerialize を持たない構造体は、FRepLayout と同様に UPROPERTY 単位で展開されて送られる */
	void SerializeStructMembers(FMeasureContext& Ctx, const UStruct* Struct, void* StructData)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Prop = *It;

			// UPROPERTY(NotReplicated) は送られない
			if (Prop->HasAnyPropertyFlags(CPF_RepSkip))
			{
				continue;
			}

			// 固定長配列(int32 Foo[4] など)は要素ごと。※UE5.5 以降は GetArrayDim()
			for (int32 Index = 0; Index < Prop->ArrayDim; ++Index)
			{
				SerializeValue(Ctx, Prop, Prop->ContainerPtrToValuePtr<void>(StructData, Index));
			}
		}
	}

	/** 構造体 1 個分。独自 NetSerialize があればそれを、なければ UPROPERTY ごとに書く */
	void SerializeStructValue(FMeasureContext& Ctx, const UScriptStruct* Struct, void* StructData)
	{
		if (Struct->StructFlags & STRUCT_NetSerializeNative)
		{
			bool bSuccess = true;
			Struct->GetCppStructOps()->NetSerialize(Ctx.Writer, Ctx.PackageMap, bSuccess, StructData);
		}
		else
		{
			SerializeStructMembers(Ctx, Struct, StructData);
		}
	}

	void SerializeValue(FMeasureContext& Ctx, const FProperty* Prop, void* ValuePtr)
	{
		FArchive& Ar = Ctx.Writer;

		// --- 構造体 ---
		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			const UScriptStruct* InnerStruct = StructProp->Struct;
			SerializeStructValue(Ctx, InnerStruct, ValuePtr);
			return;
		}

		// --- 動的配列: 配列チェック + 要素数(uint16) + 各要素 ---
		if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			FScriptArrayHelper Helper(ArrayProp, ValuePtr);
			const int32 Num = Helper.Num();

			// ※UE5.5 以降は GetElementSize()
			CheckArrayLimits(Ctx, ArrayProp->GetName(), Num, ArrayProp->Inner->ElementSize);
			WriteArrayNum(Ctx, Num);

			for (int32 Index = 0; Index < Num; ++Index)
			{
				SerializeValue(Ctx, ArrayProp->Inner, Helper.GetRawPtr(Index));
			}
			return;
		}

		// --- TMap / TSet はレプリケーション非対応(送られない) ---
		if (CastField<FMapProperty>(Prop) != nullptr || CastField<FSetProperty>(Prop) != nullptr)
		{
			return;
		}

		// --- PackageMap が無いときのオブジェクト参照 ---
		if (Ctx.PackageMap == nullptr)
		{
			if (CastField<FSoftObjectProperty>(Prop) != nullptr)
			{
				// ソフト参照はパス文字列として見積もる
				FString Path = static_cast<const FSoftObjectPtr*>(ValuePtr)->ToString();
				Ar << Path;
				return;
			}
			if (CastField<FObjectPropertyBase>(Prop) != nullptr || CastField<FInterfaceProperty>(Prop) != nullptr)
			{
				// 実体は接続ごとの NetGUID(可変長整数)
				Ctx.Writer.AddObjectRefEstimate();
				return;
			}
		}

		// --- それ以外(数値 / bool / enum / FString / FName / FText など)はエンジンと同じ関数で書く ---
		// FString は「長さ(int32) + 文字 + 終端」。ASCII だけなら 1 文字 1 バイト、
		// 日本語など非 ASCII を含むと UTF-16 で 1 文字 2 バイトになる。
		Prop->NetSerializeItem(Ar, Ctx.PackageMap, ValuePtr);
	}

	FNetBunchSizeReport MakeReport(FMeasureContext& Ctx, int32 HeaderMarginBytes)
	{
		FNetBunchSizeReport Report;
		Report.LimitBytes = NetBunchSizeCheck::GetMaxBunchBytes();

		const int64 TotalBits = Ctx.Writer.GetNumBits() + Ctx.Writer.EstimatedExtraBits;

		Report.PayloadBytes        = (TotalBits + 7) / 8;
		Report.EstimatedBunchBytes = Report.PayloadBytes + HeaderMarginBytes;
		Report.OverBytes           = Report.EstimatedBunchBytes - Report.LimitBytes;
		Report.EstimatedObjectRefs = Ctx.Writer.EstimatedObjectRefs;
		Report.bArrayLimitExceeded = Ctx.bArrayLimitExceeded;
		Report.ArrayLimitDetail    = MoveTemp(Ctx.ArrayLimitDetail);
		return Report;
	}
} // namespace

int32 NetBunchSizeCheck::GetMaxBunchBytes()
{
	return ReadCVarInt(TEXT("net.MaxConstructedPartialBunchSizeBytes"), FallbackMaxBunchBytes);
}

FNetBunchSizeReport NetBunchSizeCheck::MeasureStruct(const UScriptStruct* Struct, const void* StructData,
	int32 HeaderMarginBytes, UPackageMap* PackageMap)
{
	FMeasureWriter Writer(PackageMap);
	FMeasureContext Ctx = MakeContext(Writer, PackageMap);

	if (Struct != nullptr && StructData != nullptr)
	{
		// 書き込みのみで中身は変えないが、エンジン側 API が非 const ポインタを取るため
		SerializeStructValue(Ctx, Struct, const_cast<void*>(StructData));
	}
	return MakeReport(Ctx, HeaderMarginBytes);
}

FNetBunchSizeReport NetBunchSizeCheck::MeasureStructArray(const UScriptStruct* ElementStruct, const void* Elements,
	int32 Num, int32 ElementSize, int32 HeaderMarginBytes, UPackageMap* PackageMap)
{
	FMeasureWriter Writer(PackageMap);
	FMeasureContext Ctx = MakeContext(Writer, PackageMap);

	if (ElementStruct != nullptr && (Elements != nullptr || Num == 0))
	{
		// TArray を直接 RPC 引数にした場合と同じ: 配列チェック + 要素数(uint16) + 各要素
		CheckArrayLimits(Ctx, FString::Printf(TEXT("TArray<%s>"), *ElementStruct->GetName()), Num, ElementSize);
		WriteArrayNum(Ctx, Num);

		uint8* Base = static_cast<uint8*>(const_cast<void*>(Elements));
		for (int32 Index = 0; Index < Num; ++Index)
		{
			SerializeStructValue(Ctx, ElementStruct, Base + int64(Index) * ElementSize);
		}
	}
	return MakeReport(Ctx, HeaderMarginBytes);
}

FString FNetBunchSizeReport::ToString() const
{
	FString Result = FString::Printf(
		TEXT("Payload=%lld B, EstimatedBunch=%lld B, Limit=%lld B, Over=%lld B"),
		PayloadBytes, EstimatedBunchBytes, LimitBytes, FMath::Max<int64>(OverBytes, 0));

	if (EstimatedObjectRefs > 0)
	{
		Result += FString::Printf(TEXT(" (UObject refs estimated: %d)"), EstimatedObjectRefs);
	}
	if (bArrayLimitExceeded)
	{
		Result += TEXT("\nArray limit exceeded:\n");
		Result += ArrayLimitDetail;
	}
	return Result;
}
