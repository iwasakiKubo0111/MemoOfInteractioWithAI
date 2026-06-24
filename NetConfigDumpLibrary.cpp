#include "NetConfigDumpLibrary.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "GameFramework/GameNetworkManager.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY_STATIC(LogNetConfigDump, Log, All);

namespace
{
	// マージ済み設定(GConfig)から int を読み、見つからなければ警告を出す
	void DumpInt(const TCHAR* Section, const TCHAR* Key, const FString& IniFile, const TCHAR* IniLabel)
	{
		int32 Value = 0;
		const bool bFound = GConfig && GConfig->GetInt(Section, Key, Value, IniFile);
		if (bFound)
		{
			UE_LOG(LogNetConfigDump, Log, TEXT("  [%s] %s : %s = %d"), IniLabel, Section, Key, Value);
		}
		else
		{
			UE_LOG(LogNetConfigDump, Warning, TEXT("  [%s] %s : %s = (見つかりません / NOT FOUND)"), IniLabel, Section, Key);
		}
	}

	void DumpFloat(const TCHAR* Section, const TCHAR* Key, const FString& IniFile, const TCHAR* IniLabel)
	{
		float Value = 0.f;
		const bool bFound = GConfig && GConfig->GetFloat(Section, Key, Value, IniFile);
		if (bFound)
		{
			UE_LOG(LogNetConfigDump, Log, TEXT("  [%s] %s : %s = %f"), IniLabel, Section, Key, Value);
		}
		else
		{
			UE_LOG(LogNetConfigDump, Warning, TEXT("  [%s] %s : %s = (見つかりません / NOT FOUND)"), IniLabel, Section, Key);
		}
	}

	const TCHAR* NetModeToString(ENetMode Mode)
	{
		switch (Mode)
		{
		case NM_Standalone:      return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer:    return TEXT("ListenServer");
		case NM_Client:          return TEXT("Client");
		default:                 return TEXT("Unknown");
		}
	}
}

void UNetConfigDumpLibrary::DumpNetConfig(const UObject* WorldContextObject)
{
	UE_LOG(LogNetConfigDump, Log, TEXT("================ NET CONFIG DUMP ================"));

	// ---- 1) 設定ファイル(マージ後)の値 = ini が反映されているかの確認 ----
	// これらは Saved/Config のデルタファイルではなく、メモリ上の結合済み設定を読むので「実際に効いている値」が出る。
	UE_LOG(LogNetConfigDump, Log, TEXT("---- (1) Config file values (merged hierarchy) ----"));

	// [/Script/Engine.Player] と [/Script/OnlineSubsystemUtils.IpNetDriver] は Engine.ini 系 (GEngineIni)
	DumpInt(TEXT("/Script/Engine.Player"), TEXT("ConfiguredInternetSpeed"), GEngineIni, TEXT("EngineIni"));
	DumpInt(TEXT("/Script/Engine.Player"), TEXT("ConfiguredLanSpeed"),      GEngineIni, TEXT("EngineIni"));

	DumpInt(TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"), TEXT("MaxClientRate"),         GEngineIni, TEXT("EngineIni"));
	DumpInt(TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"), TEXT("MaxInternetClientRate"), GEngineIni, TEXT("EngineIni"));
	DumpInt(TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"), TEXT("NetServerMaxTickRate"),  GEngineIni, TEXT("EngineIni"));
	DumpInt(TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"), TEXT("MaxNetTickRate"),        GEngineIni, TEXT("EngineIni"));
	DumpInt(TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"), TEXT("LanServerMaxTickRate"),  GEngineIni, TEXT("EngineIni"));

	// [/Script/Engine.GameNetworkManager] は Game.ini 系 (GGameIni)
	DumpInt(TEXT("/Script/Engine.GameNetworkManager"), TEXT("TotalNetBandwidth"),   GGameIni, TEXT("GameIni"));
	DumpInt(TEXT("/Script/Engine.GameNetworkManager"), TEXT("MaxDynamicBandwidth"), GGameIni, TEXT("GameIni"));
	DumpInt(TEXT("/Script/Engine.GameNetworkManager"), TEXT("MinDynamicBandwidth"), GGameIni, TEXT("GameIni"));
	DumpFloat(TEXT("/Script/Engine.GameNetworkManager"), TEXT("ClientNetSendMoveDeltaTime"), GGameIni, TEXT("GameIni"));

	// ---- 2) 実行中の実値 = 実際に使われている値 ----
	UE_LOG(LogNetConfigDump, Log, TEXT("---- (2) Live runtime values ----"));

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		UE_LOG(LogNetConfigDump, Warning, TEXT("  World が取得できませんでした。"));
	}
	else
	{
		UE_LOG(LogNetConfigDump, Log, TEXT("  NetMode = %s"), NetModeToString(World->GetNetMode()));

		if (UNetDriver* NetDriver = World->GetNetDriver())
		{
			UE_LOG(LogNetConfigDump, Log, TEXT("  NetDriver class            = %s"), *NetDriver->GetClass()->GetName());
			UE_LOG(LogNetConfigDump, Log, TEXT("  NetServerMaxTickRate(live) = %d"), NetDriver->NetServerMaxTickRate);
			UE_LOG(LogNetConfigDump, Log, TEXT("  MaxClientRate(live)        = %d"), NetDriver->MaxClientRate);
			UE_LOG(LogNetConfigDump, Log, TEXT("  MaxInternetClientRate(live)= %d"), NetDriver->MaxInternetClientRate);

			// クライアント側: サーバーへの接続(ここが、ConfiguredLanSpeed と MaxClientRate を交渉した「実効上限」)
			if (UNetConnection* ServerConn = NetDriver->ServerConnection)
			{
				UE_LOG(LogNetConfigDump, Log, TEXT("  [Client->Server] CurrentNetSpeed(negotiated) = %d byte/s"),
					ServerConn->CurrentNetSpeed);
			}

			// サーバー側: 各クライアント接続の実効上限
			for (UNetConnection* ClientConn : NetDriver->ClientConnections)
			{
				if (ClientConn)
				{
					UE_LOG(LogNetConfigDump, Log, TEXT("  [Server->Client] %s CurrentNetSpeed(negotiated) = %d byte/s"),
						*ClientConn->GetName(), ClientConn->CurrentNetSpeed);
				}
			}
		}
		else
		{
			UE_LOG(LogNetConfigDump, Warning, TEXT("  アクティブな NetDriver なし(未接続 / Standalone)"));
		}
	}

	// ---- 3) GameNetworkManager の CDO 実値 ----
	if (const AGameNetworkManager* GNM = GetDefault<AGameNetworkManager>())
	{
		UE_LOG(LogNetConfigDump, Log, TEXT("---- (3) GameNetworkManager (CDO) ----"));
		UE_LOG(LogNetConfigDump, Log, TEXT("  TotalNetBandwidth   = %d"), GNM->TotalNetBandwidth);
		UE_LOG(LogNetConfigDump, Log, TEXT("  MaxDynamicBandwidth = %d"), GNM->MaxDynamicBandwidth);
		UE_LOG(LogNetConfigDump, Log, TEXT("  MinDynamicBandwidth = %d"), GNM->MinDynamicBandwidth);
	}

	UE_LOG(LogNetConfigDump, Log, TEXT("================================================"));
}
