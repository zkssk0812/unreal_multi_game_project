#include "Gameplay/VHSoundConfig.h"

#include "Network/VHSettings.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogVHSound, Log, All);

bool UVHSoundConfig::bLogPlays = false;

namespace
{
	struct FLastPlay
	{
		double Time = -100.0;
		FVector Location = FVector::ZeroVector;
	};
	TMap<EVHSound, TArray<FLastPlay>> GLastPlays;

	// vh.Sound 이름 [로그 1/0] : 효과음 확인 (내 캐릭터 위치). 이름 = EVHSound 값 이름 (예: Explosion)
	FAutoConsoleCommandWithWorldAndArgs GVHSoundCommand(TEXT("vh.Sound"),
		TEXT("개발용: vh.Sound 이름 — 그 효과음을 내 캐릭터 위치에서 재생 (이름 = EVHSound, 예: Explosion). vh.Sound log 1 = 재생 로그."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || Args.Num() == 0)
			{
				return;
			}
			if (Args[0].Equals(TEXT("log"), ESearchCase::IgnoreCase))
			{
				UVHSoundConfig::bLogPlays = Args.Num() < 2 || FCString::Atoi(*Args[1]) != 0;
				return;
			}
			const UEnum* Enum = StaticEnum<EVHSound>();
			const int64 Value = Enum->GetValueByNameString(Args[0]);
			const APawn* Pawn = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
			if (Value != INDEX_NONE)
			{
				UVHSoundConfig::Play(World, static_cast<EVHSound>(Value), Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector);
			}
		}));
}

void UVHSoundConfig::Play(const UObject* WorldContext, EVHSound Sound, const FVector& Location)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const UVHSoundConfig& Config = UVHSettings::Get().GetSoundConfig();
	const FVHSoundSlot* Slot = Config.Sounds.Find(Sound);
	if (!Slot || Slot->Sounds.Num() == 0)
	{
		return;
	}
	// 같은 소리를 짧은 시간에 같은 곳(2m 안)에서 겹쳐 내지 않는다 (화염방사기·발소리 등)
	const double Now = World->GetTimeSeconds();
	if (Slot->MinInterval > 0.f)
	{
		TArray<FLastPlay>& Recent = GLastPlays.FindOrAdd(Sound);
		Recent.RemoveAll([Now, Slot](const FLastPlay& P) { return Now - P.Time > Slot->MinInterval || Now < P.Time; });
		if (Recent.ContainsByPredicate([&Location](const FLastPlay& P) { return FVector::DistSquared(P.Location, Location) < 200.f * 200.f; }))
		{
			return;
		}
		FLastPlay& New = Recent.AddDefaulted_GetRef();
		New.Time = Now;
		New.Location = Location;
	}
	USoundBase* Wave = Slot->Sounds[FMath::RandRange(0, Slot->Sounds.Num() - 1)].LoadSynchronous();
	if (!Wave)
	{
		return;
	}
	const float Volume = Slot->Volume * Config.MasterVolume;
	const float Pitch = FMath::FRandRange(FMath::Min(Slot->PitchMin, Slot->PitchMax), FMath::Max(Slot->PitchMin, Slot->PitchMax));
	USoundAttenuation* Att = !Slot->AttenuationOverride.IsNull() ? Slot->AttenuationOverride.LoadSynchronous() : Config.Attenuation.LoadSynchronous();
	const bool b2D = Slot->b2D;
	auto Emit = [World, Location, Att, b2D](USoundBase* S, float V, float P)
	{
		if (b2D)
		{
			UGameplayStatics::PlaySound2D(World, S, V, P);
		}
		else
		{
			UGameplayStatics::PlaySoundAtLocation(World, S, Location, V, P, 0.f, Att);
		}
	};
	Emit(Wave, Volume, Pitch);
	// 겹쳐 내는 소리 (늦게 내는 겹은 타이머)
	for (const FVHSoundLayer& Layer : Slot->Layers)
	{
		USoundBase* S = Layer.Sounds.Num() > 0 ? Layer.Sounds[FMath::RandRange(0, Layer.Sounds.Num() - 1)].LoadSynchronous() : nullptr;
		if (!S)
		{
			continue;
		}
		const float V = Layer.Volume * Config.MasterVolume;
		const float P = Pitch * Layer.PitchScale;
		if (Layer.PitchEndScale > 0.f && !b2D)
		{
			// 높낮이 미끄러짐 겹: 소리 장치를 만들어 0.02초마다 높낮이를 바꾼다 (레일건 방전 "위이잉↓")
			const float PEnd = Pitch * Layer.PitchEndScale;
			const float Glide = FMath::Max(0.05f, Layer.PitchGlideSeconds);
			TWeakObjectPtr<UWorld> WeakWorld(World);
			TWeakObjectPtr<USoundBase> WeakS(S);
			TWeakObjectPtr<USoundAttenuation> WeakAtt(Att);
			auto StartGlide = [WeakWorld, WeakS, WeakAtt, Location, V, P, PEnd, Glide]()
			{
				UWorld* W = WeakWorld.Get();
				UAudioComponent* Comp = W && WeakS.IsValid() ? UGameplayStatics::SpawnSoundAtLocation(W, WeakS.Get(), Location, FRotator::ZeroRotator, V, P, 0.f, WeakAtt.Get()) : nullptr;
				if (!Comp)
				{
					return;
				}
				TWeakObjectPtr<UAudioComponent> WeakComp(Comp);
				const double Start = W->GetTimeSeconds();
				if (UVHSoundConfig::bLogPlays)
				{
					UE_LOG(LogVHSound, Log, TEXT("[효과음] 높낮이 미끄러짐 시작 %s %.2f → %.2f (%.2f초)"), *WeakS->GetName(), P, PEnd, Glide);
				}
				TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
				W->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, WeakComp, Handle, Start, P, PEnd, Glide]()
				{
					UWorld* W2 = WeakWorld.Get();
					UAudioComponent* C = WeakComp.Get();
					const float U = W2 ? FMath::Clamp(static_cast<float>(W2->GetTimeSeconds() - Start) / Glide, 0.f, 1.f) : 1.f;
					if (C)
					{
						C->SetPitchMultiplier(FMath::Lerp(P, PEnd, 1.f - FMath::Square(1.f - U)));	// 처음에 빨리 내려간다
					}
					if ((!C || U >= 1.f) && W2)
					{
						if (C)
						{
							if (UVHSoundConfig::bLogPlays)
							{
								UE_LOG(LogVHSound, Log, TEXT("[효과음] 높낮이 미끄러짐 끝 %.2f (%.2f초 뒤)"), C->PitchMultiplier, W2 ? static_cast<float>(W2->GetTimeSeconds() - Start) : 0.f);
							}
							C->FadeOut(0.3f, 0.f);	// 미끄러짐이 끝나면 사라진다 (반복음처럼 긴 파일도 빔 시간에 맞춰 끝나게)
						}
						W2->GetTimerManager().ClearTimer(*Handle);
					}
				}), 0.02f, true);
			};
			if (Layer.Delay <= 0.f)
			{
				StartGlide();
			}
			else
			{
				FTimerHandle H;
				World->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(StartGlide), Layer.Delay, false);
			}
			continue;
		}
		if (Layer.Delay <= 0.f)
		{
			Emit(S, V, P);
			continue;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TWeakObjectPtr<USoundBase> WeakS(S);
		TWeakObjectPtr<USoundAttenuation> WeakAtt(Att);
		FTimerHandle H;
		World->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WeakWorld, WeakS, WeakAtt, Location, V, P, b2D]()
		{
			if (WeakWorld.IsValid() && WeakS.IsValid())
			{
				if (b2D) { UGameplayStatics::PlaySound2D(WeakWorld.Get(), WeakS.Get(), V, P); }
				else { UGameplayStatics::PlaySoundAtLocation(WeakWorld.Get(), WeakS.Get(), Location, V, P, 0.f, WeakAtt.Get()); }
			}
		}), Layer.Delay, false);
	}
	if (bLogPlays)
	{
		UE_LOG(LogVHSound, Log, TEXT("[효과음] %s (%s +%d겹) at (%.0f, %.0f)"), *StaticEnum<EVHSound>()->GetNameStringByValue(static_cast<int64>(Sound)), *Wave->GetName(), Slot->Layers.Num(), Location.X, Location.Y);
	}
}

TArray<UAudioComponent*> UVHSoundConfig::PlayAttached(const UObject* WorldContext, EVHSound Sound, USceneComponent* Attach)
{
	TArray<UAudioComponent*> Out;
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || !Attach || World->GetNetMode() == NM_DedicatedServer)
	{
		return Out;
	}
	const UVHSoundConfig& Config = UVHSettings::Get().GetSoundConfig();
	const FVHSoundSlot* Slot = Config.Sounds.Find(Sound);
	USoundBase* Wave = Slot && Slot->Sounds.Num() > 0 ? Slot->Sounds[FMath::RandRange(0, Slot->Sounds.Num() - 1)].LoadSynchronous() : nullptr;
	if (!Wave)
	{
		return Out;
	}
	USoundAttenuation* Att = Slot->b2D ? nullptr : (!Slot->AttenuationOverride.IsNull() ? Slot->AttenuationOverride.LoadSynchronous() : Config.Attenuation.LoadSynchronous());
	const float Pitch = FMath::FRandRange(FMath::Min(Slot->PitchMin, Slot->PitchMax), FMath::Max(Slot->PitchMin, Slot->PitchMax));
	auto Spawn = [&](USoundBase* S, float V, float P) -> UAudioComponent*
	{
		return UGameplayStatics::SpawnSoundAttached(S, Attach, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
			true, V, P, 0.f, Att, nullptr, true);
	};
	if (UAudioComponent* C = Spawn(Wave, Slot->Volume * Config.MasterVolume, Pitch))
	{
		Out.Add(C);
	}
	for (const FVHSoundLayer& Layer : Slot->Layers)
	{
		USoundBase* S = Layer.Sounds.Num() > 0 ? Layer.Sounds[FMath::RandRange(0, Layer.Sounds.Num() - 1)].LoadSynchronous() : nullptr;
		if (!S || Layer.Delay > 0.f)
		{
			continue;	// (늦게 내는 겹은 이 방식에서 쓰지 않는다)
		}
		const float P = Pitch * Layer.PitchScale;
		UAudioComponent* C = Spawn(S, Layer.Volume * Config.MasterVolume, P);
		if (!C)
		{
			continue;
		}
		Out.Add(C);
		if (Layer.PitchEndScale > 0.f)
		{
			// 높낮이 미끄러짐 (충전 "위이잉↑"): 0.02초마다 높낮이를 바꾸고, 끝나면 0.3초에 사라진다
			const float PEnd = Pitch * Layer.PitchEndScale;
			const float Glide = FMath::Max(0.05f, Layer.PitchGlideSeconds);
			const double Start = World->GetTimeSeconds();
			TWeakObjectPtr<UWorld> WeakWorld(World);
			TWeakObjectPtr<UAudioComponent> WeakComp(C);
			TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
			World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, WeakComp, Handle, Start, P, PEnd, Glide]()
			{
				UWorld* W2 = WeakWorld.Get();
				UAudioComponent* A = WeakComp.Get();
				const float U = W2 ? FMath::Clamp(static_cast<float>(W2->GetTimeSeconds() - Start) / Glide, 0.f, 1.f) : 1.f;
				if (A && A->IsPlaying())
				{
					A->SetPitchMultiplier(FMath::Lerp(P, PEnd, 1.f - FMath::Square(1.f - U)));
				}
				if ((!A || !A->IsPlaying() || U >= 1.f) && W2)
				{
					if (A && A->IsPlaying() && U >= 1.f)
					{
						A->FadeOut(0.3f, 0.f);
					}
					W2->GetTimerManager().ClearTimer(*Handle);
				}
			}), 0.02f, true);
		}
	}
	if (bLogPlays)
	{
		UE_LOG(LogVHSound, Log, TEXT("[효과음] 붙여서 재생 %s (%s +%d겹)"), *StaticEnum<EVHSound>()->GetNameStringByValue(static_cast<int64>(Sound)), *Wave->GetName(), Out.Num() - 1);
	}
	return Out;
}

bool UVHSoundConfig::Has(EVHSound Sound)
{
	const FVHSoundSlot* Slot = UVHSettings::Get().GetSoundConfig().Sounds.Find(Sound);
	return Slot && Slot->Sounds.Num() > 0;
}

UAudioComponent* UVHSoundConfig::PlayLoop(const UObject* WorldContext, EVHSound Sound, USceneComponent* Attach)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || !Attach || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	const UVHSoundConfig& Config = UVHSettings::Get().GetSoundConfig();
	const FVHSoundSlot* Slot = Config.Sounds.Find(Sound);
	USoundBase* Wave = Slot && Slot->Sounds.Num() > 0 ? Slot->Sounds[FMath::RandRange(0, Slot->Sounds.Num() - 1)].LoadSynchronous() : nullptr;
	if (!Wave)
	{
		return nullptr;
	}
	USoundAttenuation* Att = !Slot->AttenuationOverride.IsNull() ? Slot->AttenuationOverride.LoadSynchronous() : Config.Attenuation.LoadSynchronous();
	const float Pitch = FMath::FRandRange(FMath::Min(Slot->PitchMin, Slot->PitchMax), FMath::Max(Slot->PitchMin, Slot->PitchMax));
	UAudioComponent* Comp = UGameplayStatics::SpawnSoundAttached(Wave, Attach, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
		true, Slot->Volume * Config.MasterVolume, Pitch, 0.f, Slot->b2D ? nullptr : Att, nullptr, true);
	if (Comp)
	{
		Comp->FadeIn(0.08f, Slot->Volume * Config.MasterVolume);
	}
	if (bLogPlays)
	{
		UE_LOG(LogVHSound, Log, TEXT("[효과음] 반복 시작 %s (%s)"), *StaticEnum<EVHSound>()->GetNameStringByValue(static_cast<int64>(Sound)), *Wave->GetName());
	}
	return Comp;
}

EVHSound UVHSoundConfig::FireSound(uint8 WeaponId)
{
	static const EVHSound ByWeapon[] = { EVHSound::None, EVHSound::SwingBaton, EVHSound::SwingSword, EVHSound::SwingBeam, EVHSound::FirePistol,
		EVHSound::None /*화염: 반복 소리*/, EVHSound::FireRailgun, EVHSound::FireTaser, EVHSound::ThrowGrenade, EVHSound::PlaceMine, EVHSound::PlaceMine };
	return WeaponId < UE_ARRAY_COUNT(ByWeapon) ? ByWeapon[WeaponId] : EVHSound::None;
}

EVHSound UVHSoundConfig::HitSound(uint8 WeaponId)
{
	// 화염방사기는 맞음 소리 없음: 서버가 1초에 30번 SC_DAMAGE 를 보내서 "퍽"이 끊임없이 났다 (불꽃 반복음 + 불붙음 이펙트로 충분)
	static const EVHSound ByWeapon[] = { EVHSound::Hit, EVHSound::HitBaton, EVHSound::HitSword, EVHSound::HitBeam, EVHSound::HitPistol,
		EVHSound::None, EVHSound::HitRailgun, EVHSound::HitTaser, EVHSound::Hit, EVHSound::Hit, EVHSound::HitTaser };
	const EVHSound S = WeaponId < UE_ARRAY_COUNT(ByWeapon) ? ByWeapon[WeaponId] : EVHSound::Hit;
	return S == EVHSound::None ? EVHSound::None : (Has(S) ? S : EVHSound::Hit);
}
