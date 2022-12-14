// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PuzzleGameMode.generated.h"

/**
 * 
 */
UCLASS()
class TPS_API APuzzleGameMode : public AGameModeBase
{
	GENERATED_BODY()
	
public:

	APuzzleGameMode();

	virtual void StartPlay() override;

public:

	UPROPERTY(EditDefaultsOnly)
	TArray<class UPaperSprite*> Sprites;

};
