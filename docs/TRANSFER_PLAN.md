# DAZI Integration Transfer Plan (Source -> MakeUofT26)

This plan defines what to copy from `../DAZI-AI-main` into this repo so the team can collaborate without dragging unnecessary files.

## Source Of Truth

- Source code currently validated: `../DAZI-AI-main`
- Target repo for team work: `./` (`MakeUofT26`)

## Files To Transfer First (Minimum Working Set)

1. `../DAZI-AI-main/examples/chat_configurable/chat_configurable.ino`
2. `../DAZI-AI-main/examples/speaker_tts_test/speaker_tts_test.ino`
3. `../DAZI-AI-main/src/ArduinoASRChat.h`
4. `../DAZI-AI-main/src/ArduinoASRChat.cpp`
5. `../DAZI-AI-main/src/ArduinoGPTChat.h`
6. `../DAZI-AI-main/src/ArduinoGPTChat.cpp`
7. `../DAZI-AI-main/src/ArduinoTTSChat.h`
8. `../DAZI-AI-main/src/ArduinoTTSChat.cpp`
9. `../DAZI-AI-main/src/Audio.h`
10. `../DAZI-AI-main/src/Audio.cpp`
11. `../DAZI-AI-main/src/AIProvider.h`
12. `../DAZI-AI-main/src/OpenAIProvider.h`
13. `../DAZI-AI-main/src/OpenAIProvider.cpp`
14. `../DAZI-AI-main/src/GeminiProvider.h`
15. `../DAZI-AI-main/src/GeminiProvider.cpp`
16. `../DAZI-AI-main/src/VisualContextManager.h`
17. `../DAZI-AI-main/src/VisualContextManager.cpp`
18. `firmware/src/RemoteMemory.h` (new backend-agnostic memory client)
19. `firmware/src/RemoteMemory.cpp` (new backend-agnostic memory client)
20. `firmware/src/ElevenLabsTTS.h` (ElevenLabs TTS client)
21. `firmware/src/ElevenLabsTTS.cpp` (ElevenLabs TTS client)
20. `../DAZI-AI-main/src/WebControl.h`
21. `../DAZI-AI-main/src/WebControl.cpp`
22. `../DAZI-AI-main/src/OpenAIVisionProxy.h`

## Files To Transfer Second (If Needed By Build)

- Decoder directories from `../DAZI-AI-main/src/`:
  - `aac_decoder/`
  - `flac_decoder/`
  - `mp3_decoder/`
  - `opus_decoder/`
  - `vorbis_decoder/`
- Metadata files:
  - `../DAZI-AI-main/library.properties`
  - `../DAZI-AI-main/keywords.txt`

## Files Not Needed In Team Repo

- `DAZI_AI_LIBRARY.zip` (kept only as archive)
- Old experimental examples unrelated to current robot path
- Large binaries / generated artifacts

## Target Layout Recommendation

1. `firmware/examples/chat_configurable/chat_configurable.ino`
2. `firmware/examples/speaker_tts_test/speaker_tts_test.ino`
3. `firmware/src/*` (all transferred `.h/.cpp`)
4. `docs/*` (runbooks + architecture notes)

## Suggested Copy Commands (after team approval)

```bash
mkdir -p firmware/examples/chat_configurable firmware/examples/speaker_tts_test firmware/src
cp ../DAZI-AI-main/examples/chat_configurable/chat_configurable.ino firmware/examples/chat_configurable/
cp ../DAZI-AI-main/examples/speaker_tts_test/speaker_tts_test.ino firmware/examples/speaker_tts_test/
cp ../DAZI-AI-main/src/{ArduinoASRChat.h,ArduinoASRChat.cpp,ArduinoGPTChat.h,ArduinoGPTChat.cpp,ArduinoTTSChat.h,ArduinoTTSChat.cpp,Audio.h,Audio.cpp,AIProvider.h,OpenAIProvider.h,OpenAIProvider.cpp,GeminiProvider.h,GeminiProvider.cpp,VisualContextManager.h,VisualContextManager.cpp,WebControl.h,WebControl.cpp,OpenAIVisionProxy.h} firmware/src/
cp -R ../DAZI-AI-main/src/{aac_decoder,flac_decoder,mp3_decoder,opus_decoder,vorbis_decoder} firmware/src/
cp ../DAZI-AI-main/{library.properties,keywords.txt} firmware/
```
