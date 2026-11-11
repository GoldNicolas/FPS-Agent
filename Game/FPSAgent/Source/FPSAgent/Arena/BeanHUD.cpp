#include "Arena/BeanHUD.h"
#include "Character/BeanCharacter.h"
#include "Character/HealthComponent.h"
#include "Engine/Canvas.h"
#include "Weapons/RifleComponent.h"

void ABeanHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	ABeanCharacter* Bean = Cast<ABeanCharacter>(GetOwningPawn());
	if (Bean != BoundBean.Get())
	{
		if (ABeanCharacter* Old = BoundBean.Get())
		{
			Old->GetRifle()->OnShotFired.RemoveAll(this);
		}
		BoundBean = Bean;
		if (Bean)
		{
			Bean->GetRifle()->OnShotFired.AddUObject(this, &ABeanHUD::HandleShot);
		}
	}

	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const float Spread = Bean ? Bean->GetRifle()->GetCurrentSpreadDegrees() : 0.f;
	const float Gap = 4.f + Spread * 6.f;
	const FLinearColor Green(0.1f, 1.f, 0.3f);
	DrawLine(CX - Gap - 8.f, CY, CX - Gap, CY, Green, 2.f);
	DrawLine(CX + Gap, CY, CX + Gap + 8.f, CY, Green, 2.f);
	DrawLine(CX, CY - Gap - 8.f, CX, CY - Gap, Green, 2.f);
	DrawLine(CX, CY + Gap, CX, CY + Gap + 8.f, Green, 2.f);

	if (Bean)
	{
		const FRifleFireState& Fire = Bean->GetRifle()->GetFireState();
		DrawText(FString::Printf(TEXT("HP %.0f   ARMOR %.0f   AMMO %d%s   SPEED %.0f   SPREAD %.2f deg"),
			Bean->GetHealth()->GetHealth(), Bean->GetHealth()->GetArmor(), Fire.Ammo,
			Fire.IsReloading() ? TEXT(" (reloading)") : TEXT(""), Bean->GetHorizontalSpeed(), Spread),
			FLinearColor::White, 20.f, Canvas->ClipY - 40.f);
	}
	if (GetWorld()->GetTimeSeconds() - LastHitTime < 0.6)
	{
		DrawText(LastHitText, FLinearColor::Red, CX + 20.f, CY + 20.f);
	}
}

void ABeanHUD::HandleShot(const FShotResult& Shot)
{
	if (Shot.Zone == EHitZone::None)
	{
		return;
	}
	LastHitText = FString::Printf(TEXT("%s %.0f%s"), *UEnum::GetDisplayValueAsText(Shot.Zone).ToString(), Shot.Damage, Shot.bKill ? TEXT("  KILL") : TEXT(""));
	LastHitTime = GetWorld()->GetTimeSeconds();
}
