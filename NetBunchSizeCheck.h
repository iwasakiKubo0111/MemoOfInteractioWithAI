// NetBunchSizeCheck.h
//
// 構造体(または構造体の TArray)をサーバー→クライアントへ送る前に、ネット送信サイズが
// UChannel::SendBunch の上限チェック (IsBunchTooLarge / CVar: net.MaxConstructedPartialBunchSizeBytes)
// を超えないかを事前に見積もるヘルパー。エンジン改造は不要。
//
// 使い方:
//   // TArray<FMyStruct> をそのまま RPC 引数で送る場合
//   const FNetBunchSizeReport Report = NetBunchSizeCheck::MeasureArray(MyArray);
//
//   // 構造体 1 個を送る場合
//   const FNetBunchSizeReport Report = NetBunchSizeCheck::Measure(MyStruct);
//
//   if (!Report.CanSend())
//   {
//       UE_LOG(LogTemp, Warning, TEXT("%s"), *Report.ToString());
//       // Report.EstimatedBunchBytes / LimitBytes / OverBytes / bArrayLimitExceeded を UI に出す
//   }
//
// 前提: UE5.3 の従来ネットコード(Iris 未使用)。

#pragma once

#include "CoreMinimal.h"

class UScriptStruct;
class UPackageMap;

/** 見積もり結果(単位はすべてバイト) */
struct FNetBunchSizeReport
{
	/** ネット用にシリアライズしたサイズ(RPC 引数の中身に相当) */
	int64 PayloadBytes = 0;

	/** PayloadBytes + ヘッダ用マージン = 送信バンチサイズの見積もり */
	int64 EstimatedBunchBytes = 0;

	/** 送信上限 = net.MaxConstructedPartialBunchSizeBytes の現在値(既定 65536) */
	int64 LimitBytes = 0;

	/** 上限からの超過量。0 以下なら上限内 */
	int64 OverBytes = 0;

	/** PackageMap なしで概算した UObject 参照の数(0 なら概算なし = 全て実測) */
	int32 EstimatedObjectRefs = 0;

	/**
	 * 動的配列が net.MaxRepArraySize(要素数) / net.MaxRepArrayMemory(要素数×メモリ上の要素サイズ)
	 * を超えている。この場合はバンチ上限とは別の理由でシリアライズ自体が失敗し、送信されない。
	 */
	bool bArrayLimitExceeded = false;
	FString ArrayLimitDetail;

	bool IsOverBunchLimit() const { return OverBytes > 0; }
	bool CanSend() const { return !IsOverBunchLimit() && !bArrayLimitExceeded; }

	FString ToString() const;
};

namespace NetBunchSizeCheck
{
	/** 現在の送信上限(バイト)。CVar net.MaxConstructedPartialBunchSizeBytes を読む */
	int32 GetMaxBunchBytes();

	/**
	 * USTRUCT 1 個を「RPC 引数として送るとき」と同じ規則でシリアライズしてサイズを測る。
	 *   - 独自 NetSerialize を持つ構造体はそれを呼ぶ
	 *   - 持たない構造体は UPROPERTY ごとに展開(NotReplicated は除外)
	 *   - メンバーの TArray は 要素数(uint16) + 各要素
	 *
	 * @param HeaderMarginBytes  バンチ/RPC ヘッダ分の余裕。RPC なら数十バイトで足りる
	 * @param PackageMap         通常は nullptr のままでよい(UObject 参照は 1 個 4 バイトで概算)。
	 *                           実測したい場合だけサーバー側の NetConnection->PackageMap を渡す
	 *                           (その場合 NetGUID の割り当てなど実送信と同じ処理が走る)
	 */
	FNetBunchSizeReport MeasureStruct(const UScriptStruct* Struct, const void* StructData,
		int32 HeaderMarginBytes = 64, UPackageMap* PackageMap = nullptr);

	/**
	 * TArray<USTRUCT> を「そのまま RPC 引数として送るとき」のサイズを測る。
	 * 配列の上限(net.MaxRepArraySize / net.MaxRepArrayMemory)も同時にチェックする。
	 */
	FNetBunchSizeReport MeasureStructArray(const UScriptStruct* ElementStruct, const void* Elements, int32 Num,
		int32 ElementSize, int32 HeaderMarginBytes = 64, UPackageMap* PackageMap = nullptr);

	template<typename TStruct>
	FNetBunchSizeReport Measure(const TStruct& Value, int32 HeaderMarginBytes = 64, UPackageMap* PackageMap = nullptr)
	{
		return MeasureStruct(TStruct::StaticStruct(), &Value, HeaderMarginBytes, PackageMap);
	}

	template<typename TStruct>
	FNetBunchSizeReport MeasureArray(const TArray<TStruct>& Array, int32 HeaderMarginBytes = 64, UPackageMap* PackageMap = nullptr)
	{
		return MeasureStructArray(TStruct::StaticStruct(), Array.GetData(), Array.Num(),
			static_cast<int32>(sizeof(TStruct)), HeaderMarginBytes, PackageMap);
	}
}
