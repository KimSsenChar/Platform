// Fill out your copyright notice in the Description page of Project Settings.


#include "PortalWall.h"
#include "Portal.h"

// Sets default values
APortalWall::APortalWall()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MESH"));
	Mesh->SetCollisionProfileName(TEXT("PortalWall"));
	RootComponent = Mesh;

	Width = 100.f;
	Height = 100.f;
}

// Called when the game starts or when spawned
void APortalWall::BeginPlay()
{
	Super::BeginPlay();
	

}

std::pair<bool, FTransform> APortalWall::ClampPortalPosition(FVector Location, TWeakObjectPtr<APortal> LinkedPortal)
{
	FTransform ClampTransform;
	FVector ClampLocation;
	FVector LocalLocation = GetTransform().InverseTransformPositionNoScale(Location);

	const float PortalWidth = 180.f;
	const float PortalHeight = 249.f;

	ClampLocation.X = 5.f;
	ClampLocation.Y = FMath::Clamp(FMath::Abs(LocalLocation.Y), 0.f, Width / 2 - PortalWidth / 2);
	ClampLocation.Z = FMath::Clamp(FMath::Abs(LocalLocation.Z), 0.f, Height / 2 - PortalHeight / 2);
	
	if (LocalLocation.Y < 0)
	{
		ClampLocation.Y *= -1.f;
	}

	if (LocalLocation.Z < 0)
	{
		ClampLocation.Z *= -1.f;
	}

	if (LinkedPortal.IsValid())
	{
		FVector LinkedPortalLocalLocation = GetTransform().InverseTransformPositionNoScale(LinkedPortal->GetActorLocation());
		bool IsOverlap = CheckOverlapLinkedPortal(ClampLocation, LinkedPortalLocalLocation);

		ClampLocation = GetTransform().TransformPositionNoScale(ClampLocation);

		ClampTransform = FTransform(GetActorRotation(), ClampLocation);

		return std::make_pair(IsOverlap, ClampTransform);
	}
	
	ClampLocation = GetTransform().TransformPositionNoScale(ClampLocation);

	ClampTransform = FTransform(GetActorRotation(), ClampLocation);

	return std::make_pair(false, ClampTransform);
}

bool APortalWall::CheckOverlapLinkedPortal(FVector PositionA, FVector PositionB)
{
	float A_X = PositionA.X;
	float A_Y = PositionA.Y;
	float A_Z = PositionA.Z;

	float B_X = PositionB.X;
	float B_Y = PositionB.Y;
	float B_Z = PositionB.Z;

	if (FMath::Abs(A_X - B_X) > 0.01f)
		return false;

	const float PortalWidth = 180.f;
	const float PortalHeight = 249.f;

	if (A_Y + PortalWidth / 2 < B_Y - PortalWidth / 2)
		return false;
	if (B_Y + PortalWidth / 2 < A_Y - PortalWidth / 2)
		return false;
	if (A_Z + PortalHeight / 2 < B_Z - PortalHeight / 2)
		return false;
	if (B_Z + PortalHeight / 2 < A_Z - PortalHeight / 2)
		return false;

	return true;
}

// Called every frame
void APortalWall::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

