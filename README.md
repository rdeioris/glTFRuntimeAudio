# glTFRuntimeAudio
Advanced audio support for glTFRuntime

Currently supported formats:

* WAV
* Ogg/Opus
* Ogg/Vorbis
* Mp3

## Examples

Loading and playing an mp3 from an url (remember to set AsBlob option in the LoaderConfig)

![image](https://github.com/user-attachments/assets/b6f0e029-d89c-49af-b253-48c563f958ae)

If your audio file is expressed in a standard glTF/JSON asset like a uri or a bufferView you can use the ```UglTFRuntimeAudioFunctionLibrary::LoadSoundFromPath``` function.
