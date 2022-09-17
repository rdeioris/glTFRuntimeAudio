// Copyright 2022, Roberto De Ioris.


#include "glTFRuntimeAudioFunctionLibrary.h"
#include "Audio.h"
THIRD_PARTY_INCLUDES_START
#include <opus.h>
#include <vorbis/codec.h>
#define MINIMP3_IMPLEMENTATION
#include <minimp3/minimp3.h>
THIRD_PARTY_INCLUDES_END


namespace glTFRuntime
{
	namespace Audio
	{
		struct FOggPage
		{
			uint8 Version;
			uint8 Flags;
			uint64 GranulePosition;
			uint32 Serial;
			uint32 Sequence;
			TArray<FglTFRuntimeBlob> Segments;
			int64 PageSize;
		};

		static bool LoadOggPage(const uint8* Data, const int64 Size, FOggPage& OggPage)
		{
			if (Size < 27)
			{
				return false;
			}

			if (FMemory::Memcmp(Data, "OggS", 4))
			{
				return false;
			}

			OggPage.Version = Data[4];
			OggPage.Flags = Data[5];
			OggPage.GranulePosition = *(reinterpret_cast<const uint64*>(Data + 6));
			OggPage.Serial = *(reinterpret_cast<const uint32*>(Data + 14));
			OggPage.Sequence = *(reinterpret_cast<const uint32*>(Data + 18));

			int32 SegmentsNum = Data[26];

			if (Size < 27 + SegmentsNum)
			{
				return false;
			}

			OggPage.Segments.Empty();

			int64 Offset = 27 + SegmentsNum;
			int32 CurrentSegmentSize = 0;
			for (int32 SegmentIndex = 0; SegmentIndex < SegmentsNum; SegmentIndex++)
			{
				uint8 SegmentSize = Data[27 + SegmentIndex];
				CurrentSegmentSize += SegmentSize;
				if (SegmentSize < 0xFF || SegmentIndex == (SegmentsNum - 1)) // end of segment
				{
					if (Size < Offset + CurrentSegmentSize)
					{
						return false;
					}

					FglTFRuntimeBlob Blob;
					Blob.Data = const_cast<uint8*>(Data) + Offset;
					Blob.Num = CurrentSegmentSize;
					OggPage.Segments.Add(Blob);
					Offset += CurrentSegmentSize;
					CurrentSegmentSize = 0;
				}
			}

			OggPage.PageSize = Offset;

			return true;
		}

		static bool LoadOggOpus(const TArray64<uint8>& Source, TArray64<uint8>& Destination, int32& Channels, int32& SampleRate)
		{
			bool bFoundOpusHead = false;
			FOggPage OggPage;
			int64 Offset = 0;
			int64 Size = Source.Num();
			uint32 Serial = 0;
			while (!bFoundOpusHead)
			{
				if (LoadOggPage(Source.GetData() + Offset, Size, OggPage))
				{
					if (OggPage.GranulePosition == 0 && OggPage.Segments.Num() == 1 && OggPage.Segments[0].Num == 19)
					{
						if (!FMemory::Memcmp(OggPage.Segments[0].Data, "OpusHead", 8))
						{
							Channels = OggPage.Segments[0].Data[9];
							SampleRate = 48000;
							Serial = OggPage.Serial;
							bFoundOpusHead = true;
						}
					}

					Offset += OggPage.PageSize;
					Size -= OggPage.PageSize;
				}
				else // invalid stream
				{
					break;
				}
			}

			if (!bFoundOpusHead || Channels < 1)
			{
				return false;
			}

			int32 OpusError = 0;
			OpusDecoder* OpusDecoderPtr = opus_decoder_create(SampleRate, Channels, &OpusError);
			if (OpusError < 0)
			{
				return false;
			}

			TArray<uint8> DecompressedSamples;
			DecompressedSamples.AddUninitialized(32768);

			while (LoadOggPage(Source.GetData() + Offset, Size, OggPage))
			{
				if (OggPage.GranulePosition > 0 && OggPage.Serial == Serial && OggPage.Segments.Num() > 0)
				{
					for (const FglTFRuntimeBlob& Blob : OggPage.Segments)
					{
						int32 FrameSize = opus_decode(OpusDecoderPtr, Blob.Data, Blob.Num, reinterpret_cast<int16*>(DecompressedSamples.GetData()), DecompressedSamples.Num(), 0);
						if (FrameSize < 0)
						{
							break;
						}

						if (FrameSize > 0)
						{
							Destination.Append(DecompressedSamples.GetData(), FrameSize * Channels * sizeof(int16));
						}
					}
				}

				Offset += OggPage.PageSize;
				Size -= OggPage.PageSize;
			}

			opus_decoder_destroy(OpusDecoderPtr);

			return true;
		}

		static bool LoadMp3(const TArray64<uint8>& Source, TArray64<uint8>& Destination, int32& Channels, int32& SampleRate)
		{
			mp3dec_t Mp3Decoder;
			mp3dec_init(&Mp3Decoder);

			mp3dec_frame_info_t Mp3FrameInfo;
			int16 Frame[MINIMP3_MAX_SAMPLES_PER_FRAME];

			int64 Offset = 0;

			while (Offset < Source.Num())
			{
				int32 NumSamples = mp3dec_decode_frame(&Mp3Decoder, Source.GetData() + Offset, Source.Num() - Offset, Frame, &Mp3FrameInfo);
				if (NumSamples <= 0)
				{
					return false;
				}

				Offset += Mp3FrameInfo.frame_bytes;

				Channels = Mp3FrameInfo.channels;
				SampleRate = Mp3FrameInfo.hz;

				Destination.Append(reinterpret_cast<uint8*>(Frame), NumSamples * sizeof(int16) * Channels);
			}

			return true;
		}

		static bool LoadOggVorbis(const TArray64<uint8>& Source, TArray64<uint8>& Destination, int32& Channels, int32& SampleRate)
		{
			TArray<uint8> VorbisHeader;
			TArray<uint8> VorbisComments;
			TArray<uint8> VorbisSetup;
			FOggPage OggPage;
			int64 Offset = 0;
			int64 Size = Source.Num();
			uint32 Serial = 0;
			while (!(VorbisHeader.Num() > 0 && VorbisComments.Num() > 0 && VorbisSetup.Num() > 0))
			{
				if (LoadOggPage(Source.GetData() + Offset, Size, OggPage))
				{
					if (OggPage.GranulePosition == 0)
					{
						for (const FglTFRuntimeBlob& Blob : OggPage.Segments)
						{
							if (Blob.Num >= 7)
							{
								if (!FMemory::Memcmp(Blob.Data, "\1vorbis", 7))
								{
									VorbisHeader.Empty();
									VorbisHeader.Append(Blob.Data, Blob.Num);
									Serial = OggPage.Serial;
								}
								else if (!FMemory::Memcmp(Blob.Data, "\3vorbis", 7))
								{
									VorbisComments.Empty();
									VorbisComments.Append(Blob.Data, Blob.Num);
								}
								else if (!FMemory::Memcmp(Blob.Data, "\5vorbis", 7))
								{
									VorbisSetup.Empty();
									VorbisSetup.Append(Blob.Data, Blob.Num);
								}
							}
						}
					}

					Offset += OggPage.PageSize;
					Size -= OggPage.PageSize;
				}
				else // invalid stream
				{
					break;
				}
			}

			if (VorbisHeader.Num() == 0 || VorbisComments.Num() == 0 || VorbisSetup.Num() == 0)
			{
				return false;
			}

			vorbis_info VorbisInfo;
			vorbis_info_init(&VorbisInfo);

			vorbis_comment VorbisComment;
			vorbis_comment_init(&VorbisComment);

			ogg_packet OggPacket = {};
			OggPacket.b_o_s = 1;
			OggPacket.bytes = VorbisHeader.Num();
			OggPacket.packet = VorbisHeader.GetData();

			if (vorbis_synthesis_headerin(&VorbisInfo, &VorbisComment, &OggPacket))
			{
				vorbis_comment_clear(&VorbisComment);
				return false;
			}

			OggPacket.bytes = VorbisComments.Num();
			OggPacket.packet = VorbisComments.GetData();

			if (vorbis_synthesis_headerin(&VorbisInfo, &VorbisComment, &OggPacket))
			{
				vorbis_comment_clear(&VorbisComment);
				return false;
			}

			OggPacket.bytes = VorbisSetup.Num();
			OggPacket.packet = VorbisSetup.GetData();

			if (vorbis_synthesis_headerin(&VorbisInfo, &VorbisComment, &OggPacket))
			{
				vorbis_comment_clear(&VorbisComment);
				return false;
			}

			vorbis_comment_clear(&VorbisComment);

			if (VorbisInfo.channels < 1 || VorbisInfo.rate <= 0)
			{
				return false;
			}

			Channels = VorbisInfo.channels;
			SampleRate = VorbisInfo.rate;

			vorbis_dsp_state VorbisDspState;

			if (vorbis_synthesis_init(&VorbisDspState, &VorbisInfo))
			{
				return false;
			}

			vorbis_block VorbisBlock;

			if (vorbis_block_init(&VorbisDspState, &VorbisBlock))
			{
				return false;
			}

			const int32 FrameSize = 4096 / Channels;

			while (LoadOggPage(Source.GetData() + Offset, Size, OggPage))
			{
				if (OggPage.GranulePosition > 0 && OggPage.Serial == Serial && OggPage.Segments.Num() > 0)
				{
					for (const FglTFRuntimeBlob& Blob : OggPage.Segments)
					{

						OggPacket = {};
						OggPacket.bytes = Blob.Num;
						OggPacket.packet = Blob.Data;
						OggPacket.granulepos = OggPacket.granulepos;

						if (vorbis_synthesis(&VorbisBlock, &OggPacket))
						{
							break;
						}

						if (vorbis_synthesis_blockin(&VorbisDspState, &VorbisBlock))
						{
							break;
						}

						float** Pcm = nullptr;

						int32 SamplesCount = vorbis_synthesis_pcmout(&VorbisDspState, &Pcm);
						while (SamplesCount > 0)
						{
							const int32 SamplesToProcess = FMath::Min<int32>(SamplesCount, FrameSize);
							for (int32 i = 0; i < SamplesToProcess; i++)
							{
								for (int32 Channel = 0; Channel < Channels; Channel++)
								{
									int16 Sample = FMath::Clamp<float>(Pcm[Channel][i], -1.0f, 1.0f) * 32767;
									Destination.Append(reinterpret_cast<const uint8*>(&Sample), sizeof(int16));
								}

							}

							vorbis_synthesis_read(&VorbisDspState, SamplesToProcess);

							SamplesCount = vorbis_synthesis_pcmout(&VorbisDspState, &Pcm);
						}
					}
				}

				Offset += OggPage.PageSize;
				Size -= OggPage.PageSize;
			}

			vorbis_block_clear(&VorbisBlock);
			vorbis_dsp_clear(&VorbisDspState);
			vorbis_info_clear(&VorbisInfo);

			return true;
		}

		static bool LoadWav(const TArray64<uint8>& Source, TArray64<uint8>& Destination, int32& Channels, int32& SampleRate)
		{
			FWaveModInfo WaveModInfo;
			if (!WaveModInfo.ReadWaveInfo(Source.GetData(), Source.Num()))
			{
				return false;
			}

			Channels = *WaveModInfo.pChannels;
			SampleRate = *WaveModInfo.pSamplesPerSec;

			Destination.Append(WaveModInfo.SampleDataStart, WaveModInfo.SampleDataSize);

			return true;
		}

		static UglTFRuntimeSoundWave* LoadSound(const TArray64<uint8>& Blob, EglTFRuntimeAudioDecoder AudioDecoder, const FglTFRuntimeAudioConfig& AudioConfig)
		{
			bool bSuccess = false;
			int32 Channels = 0;
			int32 SampleRate = 0;
			TArray64<uint8> SoundData;

			if (AudioDecoder == EglTFRuntimeAudioDecoder::Auto)
			{
				bSuccess = glTFRuntime::Audio::LoadWav(Blob, SoundData, Channels, SampleRate);

				if (!bSuccess)
				{
					bSuccess = glTFRuntime::Audio::LoadOggOpus(Blob, SoundData, Channels, SampleRate);
				}

				if (!bSuccess)
				{
					bSuccess = glTFRuntime::Audio::LoadOggVorbis(Blob, SoundData, Channels, SampleRate);
				}

				if (!bSuccess)
				{
					bSuccess = glTFRuntime::Audio::LoadMp3(Blob, SoundData, Channels, SampleRate);
				}
			}
			else if (AudioDecoder == EglTFRuntimeAudioDecoder::OggOpus)
			{
				bSuccess = glTFRuntime::Audio::LoadOggOpus(Blob, SoundData, Channels, SampleRate);
			}
			else if (AudioDecoder == EglTFRuntimeAudioDecoder::OggVorbis)
			{
				bSuccess = glTFRuntime::Audio::LoadOggVorbis(Blob, SoundData, Channels, SampleRate);
			}
			else if (AudioDecoder == EglTFRuntimeAudioDecoder::Mp3)
			{
				bSuccess = glTFRuntime::Audio::LoadMp3(Blob, SoundData, Channels, SampleRate);
			}
			else if (AudioDecoder == EglTFRuntimeAudioDecoder::Wav)
			{
				bSuccess = glTFRuntime::Audio::LoadWav(Blob, SoundData, Channels, SampleRate);
			}

			if (!bSuccess)
			{
				return nullptr;
			}

			int32 BytesPerSeconds = Channels * SampleRate * sizeof(int16);

			UglTFRuntimeSoundWave* RuntimeSound = NewObject<UglTFRuntimeSoundWave>(GetTransientPackage(), NAME_None, RF_Public);

			RuntimeSound->NumChannels = Channels;


			RuntimeSound->Duration = static_cast<float>(SoundData.Num()) / BytesPerSeconds;

			RuntimeSound->SetSampleRate(SampleRate);
			RuntimeSound->TotalSamples = SoundData.Num() / (sizeof(int16) * Channels);

			RuntimeSound->bLooping = AudioConfig.bLoop;

			RuntimeSound->Volume = AudioConfig.Volume;

			RuntimeSound->SetRuntimeAudioData(SoundData.GetData(), SoundData.Num());

			return RuntimeSound;
		}
	}
}

UglTFRuntimeSoundWave* UglTFRuntimeAudioFunctionLibrary::LoadSoundFromBlob(UglTFRuntimeAsset* Asset, EglTFRuntimeAudioDecoder AudioDecoder, const FglTFRuntimeAudioConfig& AudioConfig)
{
	if (!Asset)
	{
		return nullptr;
	}

	if (!Asset->GetParser().IsValid())
	{
		return nullptr;
	}

	const TArray64<uint8>& Blob = Asset->GetParser()->GetBlob();
	if (Blob.Num() == 0)
	{
		return nullptr;
	}

	return glTFRuntime::Audio::LoadSound(Blob, AudioDecoder, AudioConfig);
}

UglTFRuntimeSoundWave* UglTFRuntimeAudioFunctionLibrary::LoadSoundFromPath(UglTFRuntimeAsset* Asset, const TArray<FglTFRuntimePathItem>& Path, EglTFRuntimeAudioDecoder AudioDecoder, const FglTFRuntimeAudioConfig& AudioConfig)
{
	if (!Asset)
	{
		return nullptr;
	}

	if (!Asset->GetParser().IsValid())
	{
		return nullptr;
	}

	TSharedPtr<FJsonValue> JsonClip = Asset->GetParser()->GetJSONObjectFromPath(Path);
	if (!JsonClip)
	{
		return nullptr;
	}
	
	const TSharedPtr<FJsonObject>* JsonClipObject = nullptr;
	if (!JsonClip->TryGetObject(JsonClipObject))
	{
		return nullptr;
	}

	TArray64<uint8> Bytes;
	if (!Asset->GetParser()->GetJsonObjectBytes(JsonClipObject->ToSharedRef(), Bytes))
	{
		return nullptr;
	}

	return glTFRuntime::Audio::LoadSound(Bytes, AudioDecoder, AudioConfig);
}
