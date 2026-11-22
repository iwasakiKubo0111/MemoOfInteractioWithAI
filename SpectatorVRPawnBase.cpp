// SpectatorVRPawnBase.cpp

#include "SpectatorVRPawnBase.h"

#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "RenderingThread.h"        // BeginCleanup
#include "Slate/WidgetRenderer.h"   // FWidgetRenderer（UMG モジュール）
#include "Widgets/Images/SImage.h"
#include "Widgets/SCanvas.h"

namespace
{
	// Size が (0,0) なら画像の元サイズを使う
	FVector2D ResolveLayerSize(const UTexture2D* Texture, const FVector2D& Size)
	{
		return (Size.X > 0.0 && Size.Y > 0.0)
			? Size
			: FVector2D(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight());
	}
}

ASpectatorVRPawnBase::ASpectatorVRPawnBase()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASpectatorVRPawnBase::BeginPlay()
{
	Super::BeginPlay();

	// ---- 1) ウィジェット（AddToViewport はしない） ----
	if (SpectatorWidgetClass)
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (!PC)
		{
			PC = UGameplayStatics::GetPlayerController(this, 0);
		}

		// PC をオーナーにしておくと、WidgetBP 側で Get Owning Player Pawn が使える
		SpectatorWidget = PC
			? CreateWidget<UUserWidget>(PC, SpectatorWidgetClass)
			: CreateWidget<UUserWidget>(GetWorld(), SpectatorWidgetClass);
	}

	// ---- 2) FWidgetRenderer と合成先RT（作るのはこの1回だけ。描くたびに透明でクリアされる） ----
	WidgetRenderer = new FWidgetRenderer(bWidgetGammaCorrection);
	CompositeRT = FWidgetRenderer::CreateTargetFor(FVector2D(CanvasSize), TF_Bilinear, bWidgetGammaCorrection);

	// ---- 3) Spectator Screen の設定（この1回だけ） ----
	if (bOverlayOnVRView)
	{
		// VR映像（全画面）を先に描き、その上に RT（全画面）を RT のアルファで重ねる
		UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenModeTexturePlusEyeLayout(
			FVector2D(0.0, 0.0), FVector2D(1.0, 1.0),   // VR映像を表示する範囲（0〜1）
			FVector2D(0.0, 0.0), FVector2D(1.0, 1.0),   // RT を表示する範囲（0〜1）
			/*bDrawEyeFirst*/ true,
			/*bClearBlack*/   true,
			/*bUseAlpha*/     true);
		UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::TexturePlusEye);
	}
	else
	{
		UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::Texture);
	}
	UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenTexture(CompositeRT.Get());

	// 初回の描画は次の Tick で行う（このとき WidgetBP の Event Construct が走る）
	RequestSpectatorRedraw(true);
}

void ASpectatorVRPawnBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Spectator Screen を既定の表示に戻しておく
	if (GEngine)
	{
		UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::SingleEyeCroppedToFill);
	}

	if (CompositeRoot.IsValid())
	{
		CompositeRoot->ClearChildren();
		CompositeRoot.Reset();
	}

	if (WidgetRenderer)
	{
		BeginCleanup(WidgetRenderer); // レンダースレッドが使い終わってから破棄される（delete はしない）
		WidgetRenderer = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void ASpectatorVRPawnBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FlushRedraw();
}

void ASpectatorVRPawnBase::RequestSpectatorRedraw(bool /*bWidgetChanged*/)
{
	bDirty = true;
}

void ASpectatorVRPawnBase::SetLayerPositions(FVector2D NewPosA, FVector2D NewPosB)
{
	PosA = NewPosA;
	PosB = NewPosB;
	RequestSpectatorRedraw(false);
}

void ASpectatorVRPawnBase::RebuildComposite()
{
	// 前回のツリーからウィジェットを外しておく
	if (CompositeRoot.IsValid())
	{
		CompositeRoot->ClearChildren();
	}

	TSharedRef<SCanvas> Canvas = SNew(SCanvas);

	// 追加した順に奥から描かれる：画像A → 画像B → ウィジェット（最前面）
	if (TextureA)
	{
		const FVector2D DrawSize = ResolveLayerSize(TextureA.Get(), SizeA);
		BrushA.SetResourceObject(TextureA.Get());
		BrushA.ImageSize = DrawSize;

		Canvas->AddSlot()
			.Position(PosA)
			.Size(DrawSize)
			[
				SNew(SImage).Image(&BrushA)
			];
	}

	if (TextureB)
	{
		const FVector2D DrawSize = ResolveLayerSize(TextureB.Get(), SizeB);
		BrushB.SetResourceObject(TextureB.Get());
		BrushB.ImageSize = DrawSize;

		Canvas->AddSlot()
			.Position(PosB)
			.Size(DrawSize)
			[
				SNew(SImage).Image(&BrushB)
			];
	}

	if (SpectatorWidget)
	{
		Canvas->AddSlot()
			.Position(WidgetPos)
			.Size(FVector2D(WidgetSize))
			[
				SpectatorWidget->TakeWidget()
			];
	}

	CompositeRoot = Canvas;
}

void ASpectatorVRPawnBase::FlushRedraw()
{
	if (!bDirty || !WidgetRenderer || !CompositeRT)
	{
		return;
	}
	bDirty = false;

	// 最新の位置・画像で Slate のツリーを組み直し（軽い処理）、同じRTに上書きで描く
	RebuildComposite();
	WidgetRenderer->DrawWidget(CompositeRT.Get(), CompositeRoot.ToSharedRef(), FVector2D(CanvasSize), 0.f);
}