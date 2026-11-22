// SpectatorVRPawnBase.h
// UE5.3 / VRTemplate の VRPawn の親クラス用サンプル（v2: Slate で合成する版）
//  - 画像A・画像B・ウィジェットを Slate（FWidgetRenderer）で1枚のRTにまとめて描き、Spectator Screen に表示する
//    Slate はアルファと sRGB を正しく扱うので、テクスチャの sRGB は通常どおりオンのままでよい
//  - bOverlayOnVRView = true なら VR 映像の上に重ねる（TexturePlusEye + アルファ）
//  - RT は最初に1回だけ作り、SetSpectatorScreenTexture も BeginPlay で1回だけ呼ぶ
//  - 再描画は「変化があった時」だけ（RequestSpectatorRedraw → 次の Tick でまとめて1回）

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Styling/SlateBrush.h"
#include "SpectatorVRPawnBase.generated.h"

class SCanvas;
class UTextureRenderTarget2D;
class UTexture2D;
class UUserWidget;
class FWidgetRenderer;

UCLASS()
class ASpectatorVRPawnBase : public APawn
{
	GENERATED_BODY()

public:
	ASpectatorVRPawnBase();

	/**
	 * 表示内容が変わったときに呼ぶ（WidgetBP でテキストを変えた直後など）。
	 * ここでは「再描画の依頼」を受け付けるだけで、実際の描画は次の Tick でまとめて1回だけ行う。
	 * @param bWidgetChanged  前の版との互換用に残している引数（今は常に全体を描き直す）
	 */
	UFUNCTION(BlueprintCallable, Category = "Spectator")
	void RequestSpectatorRedraw(bool bWidgetChanged = true);

	/** 画像A/Bの位置を変える（「位置変更 → 再合成」の確認用） */
	UFUNCTION(BlueprintCallable, Category = "Spectator")
	void SetLayerPositions(FVector2D NewPosA, FVector2D NewPosB);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * true : VR映像の上に重ねる（透明部分から VR 映像が見える。TexturePlusEye + アルファ）
	 * false: このRTだけを表示する（透明部分は黒。Texture モード）
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Spectator")
	bool bOverlayOnVRView = true;

	/** 合成先RTの解像度 */
	UPROPERTY(EditDefaultsOnly, Category = "Spectator")
	FIntPoint CanvasSize = FIntPoint(1920, 1080);

	/** 画像A（例: 中央がくり抜かれたアルファ画像）。sRGB はオンのままでよい */
	UPROPERTY(EditDefaultsOnly, Category = "Spectator|Layers")
	TObjectPtr<UTexture2D> TextureA;

	UPROPERTY(EditAnywhere, Category = "Spectator|Layers")
	FVector2D PosA = FVector2D::ZeroVector;

	/** 画像Aの描画サイズ。(0,0) なら画像の元サイズ */
	UPROPERTY(EditAnywhere, Category = "Spectator|Layers")
	FVector2D SizeA = FVector2D::ZeroVector;

	/** 画像B（例: くり抜き部分に重ねる目盛り）。sRGB はオンのままでよい */
	UPROPERTY(EditDefaultsOnly, Category = "Spectator|Layers")
	TObjectPtr<UTexture2D> TextureB;

	UPROPERTY(EditAnywhere, Category = "Spectator|Layers")
	FVector2D PosB = FVector2D::ZeroVector;

	/** 画像Bの描画サイズ。(0,0) なら画像の元サイズ */
	UPROPERTY(EditAnywhere, Category = "Spectator|Layers")
	FVector2D SizeB = FVector2D::ZeroVector;

	/** Spectator に出すウィジェット（UUserWidget を直接継承した WidgetBP） */
	UPROPERTY(EditDefaultsOnly, Category = "Spectator|Widget")
	TSubclassOf<UUserWidget> SpectatorWidgetClass;

	/** ウィジェットの描画サイズ。WidgetBP のデザイナーも同じサイズにしておく */
	UPROPERTY(EditDefaultsOnly, Category = "Spectator|Widget")
	FIntPoint WidgetSize = FIntPoint(600, 200);

	/** RT 上のウィジェットの位置（左上） */
	UPROPERTY(EditAnywhere, Category = "Spectator|Widget")
	FVector2D WidgetPos = FVector2D(60.0, 60.0);

	/** 合成全体のガンマ補正。色が暗すぎる/白っぽい場合に切り替える（BeginPlay 時に反映） */
	UPROPERTY(EditDefaultsOnly, Category = "Spectator|Widget")
	bool bWidgetGammaCorrection = true;

private:
	void RebuildComposite();
	void FlushRedraw();

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> SpectatorWidget;

	/** 合成先RT（＝Spectator Screen に渡すテクスチャ） */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> CompositeRT;

	/** 合成用の Slate ツリー（画像A・画像B・ウィジェットを SCanvas に並べたもの） */
	TSharedPtr<SCanvas> CompositeRoot;

	FSlateBrush BrushA;
	FSlateBrush BrushB;

	FWidgetRenderer* WidgetRenderer = nullptr;

	bool bDirty = false;
};