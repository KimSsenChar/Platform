﻿// Copyright Epic Games, Inc. All Rights Reserved.

#include "TPSCharacter.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Blueprint/UserWidget.h"
#include "MenuSystem/InGameMenu.h"
#include "Portal.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "UObject/UObjectGlobals.h"
#include "PortalWall.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Sound/SoundCue.h"
#include "LaserCube.h"


ATPSCharacter::ATPSCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(32.f, 88.0f);
	GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
	GetMesh()->SetCollisionProfileName(TEXT("NoCollision"));
	// set our turn rates for input
	BaseTurnRate = 1.f;
	BaseLookUpRate = 1.f;

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->RotationRate = FRotator(0.0f, -1.0f, 0.0f); // ...at this rotation rate
	GetCharacterMovement()->JumpZVelocity = 550.f;
	GetCharacterMovement()->AirControl = 0.15f;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->MaxAcceleration = 10000.f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->FallingLateralFriction = 0.2f;
	GetCharacterMovement()->BrakingDecelerationFalling = 10.f;
	GetCharacterMovement()->GravityScale = 2.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm


	FPSCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FPSCamera"));
	FPSCamera->SetupAttachment(RootComponent);
	FPSCamera->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	FPSCamera->bUsePawnControlRotation = true;
	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named MyCharacter (to avoid direct content references in C++)

	PhysicsHandle = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("Handle"));

}

void ATPSCharacter::BeginPlay()
{
	Super::BeginPlay();

	ActiveFPSCamera();
	InitLerpSetting();
}

void ATPSCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	GetWorld()->GetTimerManager().ClearTimer(CameraFOVLerpTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(GrabLocAndRotTimerHandle);
}

void ATPSCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (IsGrab)
	{
		FVector GrabedActorLocation = GrabedComponent->GetComponentLocation();
		FVector CharactrLocation = GetActorLocation();
		float Dist = FVector::Dist(GrabedActorLocation, CharactrLocation);
		if (Dist > 200)
			GrabActor();


		GrabRotator = FRotator(0.f, GetActorRotation().Yaw, 0.f);
		GrabLocation = FPSCamera->GetComponentLocation() + FPSCamera->GetForwardVector() * 130.f;
		PhysicsHandle->SetTargetLocationAndRotation(GrabLocation, GrabRotator);
	}
}

//////////////////////////////////////////////////////////////////////////
// Input

void ATPSCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// Set up gameplay key bindings
	check(PlayerInputComponent);
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	PlayerInputComponent->BindAxis("MoveForward", this, &ATPSCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &ATPSCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &ATPSCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &ATPSCharacter::LookUpAtRate);

	// handle touch devices
	PlayerInputComponent->BindTouch(IE_Pressed, this, &ATPSCharacter::TouchStarted);
	PlayerInputComponent->BindTouch(IE_Released, this, &ATPSCharacter::TouchStopped);

	// VR headset functionality
	PlayerInputComponent->BindAction("ResetVR", IE_Pressed, this, &ATPSCharacter::OnResetVR);

}

void ATPSCharacter::SpawnPortalA()
{
	auto World = GetWorld();
	if (World == nullptr) return;

	FHitResult HitResult;
	FVector Start = FPSCamera->GetComponentLocation();
	FVector End = Start + FPSCamera->GetForwardVector() * 10000.f;
	FCollisionQueryParams QueryParam = FCollisionQueryParams(NAME_None, false, this);

	bool Result = World->LineTraceSingleByChannel(HitResult,
		Start,
		End,
		ECollisionChannel::ECC_GameTraceChannel1,
		QueryParam);

	if (Result && HitResult.IsValidBlockingHit() == false) return;

	APortalWall* PortalWall = Cast<APortalWall>(HitResult.GetActor());
	if (PortalWall == nullptr) return;

	std::pair<bool, FTransform> ClampResult = PortalWall->ClampPortalPosition(HitResult.Location, PortalB);
	bool CanSpawn = ClampResult.first;
	FTransform ClampTransform = ClampResult.second;

	if (CanSpawn == false) return;

	if (PortalA.IsValid()) PortalA->Destroy();

	FName Path = TEXT("Class'/Game/Portal/BP_Portal.BP_Portal_C'");
	UClass* BP_PortalClass = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), NULL, *Path.ToString()));

	APortal* Portal = World->SpawnActorDeferred<APortal>(BP_PortalClass, ClampTransform);
	if (Portal)
	{
		PortalA = Portal;
		Portal->PortalA = true;
		Portal->LinkPortal(PortalB);
		Portal->FinishSpawning(ClampTransform);
	}

	if (SC_PortalA)
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), SC_PortalA, GetActorLocation());

}

void ATPSCharacter::SpawnPortalB()
{
	UWorld* World = GetWorld();
	if (World == nullptr) return;

	FHitResult HitResult;
	FVector Start = FPSCamera->GetComponentLocation();
	FVector End = Start + FPSCamera->GetForwardVector() * 10000.f;
	FCollisionQueryParams QueryParam = FCollisionQueryParams(NAME_None, false, this);

	bool Result = World->LineTraceSingleByChannel(HitResult,
		Start,
		End,
		ECollisionChannel::ECC_GameTraceChannel1,
		QueryParam);

	if (Result && HitResult.IsValidBlockingHit() == false) return;

	APortalWall* PortalWall = Cast<APortalWall>(HitResult.GetActor());
	if (PortalWall == nullptr) return;

	std::pair<bool, FTransform> ClampResult = PortalWall->ClampPortalPosition(HitResult.Location, PortalA);
	bool CanSpawn = ClampResult.first;
	FTransform ClampTransform = ClampResult.second;

	if (CanSpawn == false) return;

	if (PortalB.IsValid()) PortalB->Destroy();

	FName Path = TEXT("Class'/Game/Portal/BP_Portal.BP_Portal_C'");
	UClass* BP_PortalClass = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), NULL, *Path.ToString()));

	APortal* Portal = World->SpawnActorDeferred<APortal>(BP_PortalClass, ClampTransform);
	if (Portal)
	{
		PortalB = Portal;
		Portal->PortalA = false;
		Portal->LinkPortal(PortalA);
		Portal->FinishSpawning(ClampTransform);
	}

	if (SC_PortalB)
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), SC_PortalB, GetActorLocation());
}

void ATPSCharacter::GrabActor()
{
	if (IsGrab == false)
	{
		FHitResult HitResult;
		FVector Start = FPSCamera->GetComponentLocation();
		FVector End = Start + FPSCamera->GetForwardVector() * 200.f;
		FCollisionQueryParams QueryParam = FCollisionQueryParams(NAME_None, false, this);
		bool Result = GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECollisionChannel::ECC_GameTraceChannel6, QueryParam);

		if (Result == false) return;

		ALaserCube* ReflectCube = Cast<ALaserCube>(HitResult.Actor);
		if (ReflectCube)
		{
			FVector startLoc = ReflectCube->GetActorLocation();
			FVector endLoc = FPSCamera->GetComponentLocation() + FPSCamera->GetForwardVector() * 180.f - FPSCamera->GetUpVector() * 10.f;
			FRotator startRot = ReflectCube->GetActorRotation();
			FRotator endRot = FRotator(0.f, GetActorRotation().Yaw, 0.f);
			SetGrabLocAndRotTimer(HitResult, startLoc, endLoc, startRot, endRot);
		}
		else
		{
			AActor* HitActor = HitResult.GetActor();
			FVector startLoc = HitActor->GetActorLocation();
			FVector endLoc = FPSCamera->GetComponentLocation() + FPSCamera->GetForwardVector() * 180.f;
			FRotator startRot = HitActor->GetActorRotation();
			FRotator endRot = startRot;
			SetGrabLocAndRotTimer(HitResult, startLoc, endLoc, startRot, endRot);
		}
	}
	else
	{
		PhysicsHandle->ReleaseComponent();
		GrabedComponent.Reset();
		IsGrab = false;
	}
}

void ATPSCharacter::InitLerpSetting()
{
	LerpTime = 0.05f;
	IntervalTime = LerpTime / 16;
}

void ATPSCharacter::SetCameraFOVTimer(float startFOV, float targetFOV)
{
	CameraFOVLerpTimerDelegate = FTimerDelegate::CreateUObject(this, &ATPSCharacter::LerpCameraFOV, startFOV, targetFOV);
	GetWorld()->GetTimerManager().SetTimer(CameraFOVLerpTimerHandle, CameraFOVLerpTimerDelegate, IntervalTime, true);
}

void ATPSCharacter::LerpCameraFOV(float startFOV, float targetFOV)
{
	FOVCurrentTime += IntervalTime;
	float alpha = FOVCurrentTime / LerpTime;
	FPSCamera->SetFieldOfView(FMath::Lerp(startFOV, targetFOV, alpha));

	if (FOVCurrentTime >= LerpTime)
	{
		FPSCamera->SetFieldOfView(targetFOV);
		FOVCurrentTime = 0.f;
		GetWorld()->GetTimerManager().ClearTimer(CameraFOVLerpTimerHandle);
	}
}

void ATPSCharacter::SetGrabLocAndRotTimer(FHitResult hitResult, FVector startLoc, FVector endLoc, FRotator startRot, FRotator endRot)
{
	GrabLocAndRotTimerDelegate = FTimerDelegate::CreateUObject(this, &ATPSCharacter::LerpGrabLocAndRot, hitResult, startLoc, endLoc, startRot, endRot);
	GetWorld()->GetTimerManager().SetTimer(GrabLocAndRotTimerHandle, GrabLocAndRotTimerDelegate, IntervalTime, true);
}

void ATPSCharacter::LerpGrabLocAndRot(FHitResult hitResult, FVector startLoc, FVector endLoc, FRotator startRot, FRotator endRot)
{
	GrabCurrentTime += IntervalTime;
	float alpha = GrabCurrentTime / LerpTime;
	hitResult.Actor->SetActorLocation(FMath::Lerp(startLoc, endLoc, alpha));
	hitResult.Actor->SetActorRelativeRotation(FMath::Lerp(startRot, endRot, alpha));

	if (GrabCurrentTime >= LerpTime)
	{
		hitResult.Actor->SetActorLocation(endLoc);
		hitResult.Actor->SetActorRelativeRotation(endRot);
		GrabCurrentTime = 0.f;
		GetWorld()->GetTimerManager().ClearTimer(GrabLocAndRotTimerHandle);
		SetGrabSetting(hitResult);
	}
}

void ATPSCharacter::SetGrabSetting(FHitResult hitResult)
{
	GrabLocation = FPSCamera->GetComponentLocation() + FPSCamera->GetForwardVector() * 130.f;
	GrabRotator = FRotator(0.f, GetActorRotation().Yaw, 0.f);
	GrabedComponent = hitResult.GetComponent();
	PhysicsHandle->GrabComponentAtLocationWithRotation(GrabedComponent.Get(), NAME_None, GrabLocation, GrabRotator);
	IsGrab = true;
}

void ATPSCharacter::ActiveFPSCamera()
{
	GetMesh()->SetOwnerNoSee(true);
	FPSCamera->SetActive(true);
	FollowCamera->SetActive(false);
	this->bUseControllerRotationYaw = true;
	IsFPS = true;
}

void ATPSCharacter::OnResetVR()
{
	// If TPS is added to a project via 'Add Feature' in the Unreal Editor the dependency on HeadMountedDisplay in TPS.Build.cs is not automatically propagated
	// and a linker error will result.
	// You will need to either:
	//		Add "HeadMountedDisplay" to [YourProject].Build.cs PublicDependencyModuleNames in order to build successfully (appropriate if supporting VR).
	// or:
	//		Comment or delete the call to ResetOrientationAndPosition below (appropriate if not supporting VR)
	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition();
}

void ATPSCharacter::TouchStarted(ETouchIndex::Type FingerIndex, FVector Location)
{
	Jump();
}

void ATPSCharacter::TouchStopped(ETouchIndex::Type FingerIndex, FVector Location)
{
	StopJumping();
}

void ATPSCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void ATPSCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

void ATPSCharacter::MoveForward(float Value)
{
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, Value);
	}

	DirectionForward = FMath::Clamp(Value, -1.f, 1.f);
}

void ATPSCharacter::MoveRight(float Value)
{
	if ( (Controller != nullptr) && (Value != 0.0f) )
	{
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
	
		// get right vector 
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		// add movement in that direction
		AddMovementInput(Direction, Value);
	}

	DirectionRight = FMath::Clamp(Value, -1.f, 1.f);
}
