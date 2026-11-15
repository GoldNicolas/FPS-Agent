#include "Learning/CombatStats.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	double Ratio(double Num, double Den) { return Den > 0.0 ? Num / Den : 0.0; }
}

namespace FPSCombatStats
{
	void FWindow::Add(const FEpisodeRecord& R)
	{
		++Episodes;
		Clears += R.bCleared ? 1 : 0;
		Deaths += R.bDied ? 1 : 0;
		Timeouts += R.bTimedOut ? 1 : 0;
		ReturnSum += R.Return;
		for (int32 i = 0; i < FPSCombatReward::TermCount; ++i) { TermSums[i] += R.Terms[i]; }
		ClearSecondsSum += R.bCleared ? R.Seconds : 0.0;
		Kills += R.Kills;
		Shots += R.Shots;
		Hits += R.Hits;
		Headshots += R.Headshots;
		BotSpeedSum += R.BotSpeedCmS;
		if (!R.bReview && R.Level >= TopLevel - 1e-4f)
		{
			++TopEpisodes;
			TopClears += R.bCleared ? 1 : 0;
		}
	}

	double FWindow::ClearRate() const { return Ratio(Clears, Episodes); }
	double FWindow::ClearRateTop() const { return Ratio(TopClears, TopEpisodes); }
	double FWindow::DeathRate() const { return Ratio(Deaths, Episodes); }
	double FWindow::TimeoutRate() const { return Ratio(Timeouts, Episodes); }
	double FWindow::AvgReturn() const { return Ratio(ReturnSum, Episodes); }
	double FWindow::AvgTerm(int32 Term) const { return Term >= 0 && Term < FPSCombatReward::TermCount ? Ratio(TermSums[Term], Episodes) : 0.0; }
	double FWindow::Accuracy() const { return Ratio(static_cast<double>(Hits), static_cast<double>(Shots)); }
	double FWindow::HeadshotRate() const { return Ratio(static_cast<double>(Headshots), static_cast<double>(Hits)); }
	double FWindow::AvgClearSeconds() const { return Ratio(ClearSecondsSum, Clears); }
	double FWindow::KillsPerEpisode() const { return Ratio(static_cast<double>(Kills), Episodes); }
	double FWindow::AvgBotSpeed() const { return Ratio(BotSpeedSum, Episodes); }

	FString LogLine(const FWindow& W, double GameTime, const FLevelSummary& Level, double DecisionsPerSecond)
	{
		FString Line = FString::Printf(
			TEXT("CombatStats: gameTime=%.0fs episodes=%d clear=%.1f%% died=%.1f%% timeout=%.1f%% avgReturn=%.3f kills/ep=%.2f acc=%.1f%% hs=%.1f%% clearTime=%.1fs level=%.2f(%.2f..%.2f) decisions/s(wall)=%.0f clearTop=%.1f%%(n=%d) botSpeed=%.0fcm/s terms:"),
			GameTime, W.Episodes, 100.0 * W.ClearRate(), 100.0 * W.DeathRate(), 100.0 * W.TimeoutRate(), W.AvgReturn(),
			W.KillsPerEpisode(), 100.0 * W.Accuracy(), 100.0 * W.HeadshotRate(), W.AvgClearSeconds(), Level.Mean, Level.Min, Level.Max,
			DecisionsPerSecond, 100.0 * W.ClearRateTop(), W.TopEpisodes, W.AvgBotSpeed());
		for (int32 i = 0; i < FPSCombatReward::TermCount; ++i)
		{
			Line += FString::Printf(TEXT(" %s=%.3f"), FPSCombatReward::TermName(static_cast<FPSCombatReward::ETerm>(i)), W.AvgTerm(i));
		}
		return Line;
	}

	FString CsvHeader()
	{
		FString H = TEXT("gameTime,episodes,clearRate,deathRate,timeoutRate,avgReturn,killsPerEpisode,accuracy,headshotRate,avgClearSeconds,levelMean,levelMin,levelMax,decisionsPerSecond");
		for (int32 i = 0; i < FPSCombatReward::TermCount; ++i)
		{
			H += FString::Printf(TEXT(",term_%s"), FPSCombatReward::TermName(static_cast<FPSCombatReward::ETerm>(i)));
		}
		H += TEXT(",clearRateTop,episodesTop,botSpeed");
		return H;
	}

	FString CsvRow(const FWindow& W, double GameTime, const FLevelSummary& Level, double DecisionsPerSecond)
	{
		FString R = FString::Printf(TEXT("%.1f,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.1f"),
			GameTime, W.Episodes, W.ClearRate(), W.DeathRate(), W.TimeoutRate(), W.AvgReturn(), W.KillsPerEpisode(), W.Accuracy(),
			W.HeadshotRate(), W.AvgClearSeconds(), Level.Mean, Level.Min, Level.Max, DecisionsPerSecond);
		for (int32 i = 0; i < FPSCombatReward::TermCount; ++i)
		{
			R += FString::Printf(TEXT(",%.4f"), W.AvgTerm(i));
		}
		R += FString::Printf(TEXT(",%.4f,%d,%.1f"), W.ClearRateTop(), W.TopEpisodes, W.AvgBotSpeed());
		return R;
	}

	FString EvalJson(const FWindow& W, const FString& Stage, float Level, int32 Seed, const FString& Snapshots, float PromotionClearRate, const FString& Mobility)
	{
		const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("stage"), Stage);
		Obj->SetNumberField(TEXT("level"), Level);
		Obj->SetNumberField(TEXT("seed"), Seed);
		Obj->SetStringField(TEXT("snapshots"), Snapshots);
		if (!Mobility.IsEmpty()) { Obj->SetStringField(TEXT("mobility"), Mobility); } // D-033 mobility line
		Obj->SetNumberField(TEXT("episodes"), W.Episodes);
		Obj->SetNumberField(TEXT("clearRate"), W.ClearRate());
		Obj->SetNumberField(TEXT("deathRate"), W.DeathRate());
		Obj->SetNumberField(TEXT("timeoutRate"), W.TimeoutRate());
		Obj->SetNumberField(TEXT("avgReturn"), W.AvgReturn());
		Obj->SetNumberField(TEXT("killsPerEpisode"), W.KillsPerEpisode());
		Obj->SetNumberField(TEXT("accuracy"), W.Accuracy());
		Obj->SetNumberField(TEXT("headshotRate"), W.HeadshotRate());
		Obj->SetNumberField(TEXT("avgClearSeconds"), W.AvgClearSeconds());
		Obj->SetNumberField(TEXT("botSpeed"), W.AvgBotSpeed());
		Obj->SetNumberField(TEXT("promotionClearRate"), PromotionClearRate);
		Obj->SetBoolField(TEXT("passed"), W.Episodes > 0 && W.ClearRate() >= PromotionClearRate);
		const TSharedRef<FJsonObject> Terms = MakeShared<FJsonObject>();
		for (int32 i = 0; i < FPSCombatReward::TermCount; ++i)
		{
			Terms->SetNumberField(FPSCombatReward::TermName(static_cast<FPSCombatReward::ETerm>(i)), W.AvgTerm(i));
		}
		Obj->SetObjectField(TEXT("terms"), Terms);
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}
}
