// Copyright 2022, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeSoundWave.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "glTFRuntimeAudioFunctionLibrary.generated.h"

UENUM()
enum class EglTFRuntimeAudioDecoder : uint8
{
	Auto,
	OggOpus,
	OggVorbis,
	Mp3,
	Wav
};

/**
 * 
 */
UCLASS()
class GLTFRUNTIMEAUDIO_API UglTFRuntimeAudioFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "AudioConfig", AutoCreateRefTerm = "AudioConfig"), Category = "glTFRuntimeAudio")
	static UglTFRuntimeSoundWave* LoadSoundFromBlob(UglTFRuntimeAsset* Asset, EglTFRuntimeAudioDecoder AudioDecoder, const FglTFRuntimeAudioConfig& AudioConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "AudioConfig", AutoCreateRefTerm = "Path,AudioConfig"), Category = "glTFRuntimeAudio")
	static UglTFRuntimeSoundWave* LoadSoundFromPath(UglTFRuntimeAsset* Asset, const TArray<FglTFRuntimePathItem>& Path, EglTFRuntimeAudioDecoder AudioDecoder, const FglTFRuntimeAudioConfig& AudioConfig);
};
