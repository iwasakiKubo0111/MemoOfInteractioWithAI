#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NetConfigDumpLibrary.generated.h"

/**
 * ネット関連の設定値を以下の3系統でログ出力するユーティリティ。
 *   1) 設定ファイル(マージ後 = Base+Default+Platform+Saved を結合した実値) … ini が反映されているかの確認
 *   2) 実行中の実値(アクティブな NetDriver / Connection)               … 実際に使われている値
 *   3) GameNetworkManager の CDO 実値
 *
 * Blueprint から "Dump Net Config" ノードで呼べる(BeginPlay や任意のキー入力に繋ぐと確認しやすい)。
 * 出力は Output Log の "LogNetConfigDump" カテゴリ。パッケージ後は Saved/Logs のログファイルに出る。
 *
 * ※ YOURPROJECT_API は自分のモジュール名に合わせて書き換えること(例: プロジェクト名 MyGame なら MYGAME_API)。
 *   プロジェクト内の既存クラスのヘッダにある "○○_API" と同じものを使えば確実。
 */
UCLASS()
class YOURPROJECT_API UNetConfigDumpLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** すべての関連ネット設定値をログ(LogNetConfigDump)へ出力する。 */
	UFUNCTION(BlueprintCallable, Category = "Net Debug", meta = (WorldContext = "WorldContextObject"))
	static void DumpNetConfig(const UObject* WorldContextObject);
};
