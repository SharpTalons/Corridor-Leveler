Corridor Leveler
================

Keeps an audio source at a target level. Quiet passages are lifted, loud ones lowered,
silence and room noise are never boosted, and the output never goes past the ceiling.

WHERE THINGS WERE INSTALLED
  OBS filter     C:\ProgramData\obs-studio\plugins\corridor-leveler
  VST3           C:\Program Files\Common Files\VST3\Corridor Leveler.vst3
  2.x plugin     C:\Program Files\VSTPlugins\Corridor Leveler.dll
  Standalone     C:\Program Files\Corridor Leveler\Corridor Leveler.exe

USING IT IN OBS
  1. Restart OBS.
  2. Right-click your microphone in the Audio Mixer > Filters > + > Corridor Leveler.
     (This is the native filter. It has sliders in OBS's filter panel.)
  3. For the full plugin window with the level graph, use + > VST 2.x Plug-in instead and
     pick "Corridor Leveler" from the list, then press "Open Plug-in Interface".
  Put it after Noise Suppression and before any compressor or EQ.

GOOD STARTING POINT FOR A STREAM VOICE
  Target -20 dB RMS, Threshold -48 dB RMS, Response 50, Range 12 dB, Ceiling -1 dBFS.
  Set your sound card so your loudest shouts peak around -10 to -6 dBFS. Corridor can't undo
  clipping that already happened before OBS.

TRY IT WITH YOUR MIC
  Open "Corridor Leveler" from the Start menu, choose your microphone under Options >
  Audio/MIDI Settings, and talk. Use headphones if you turn on the output.

LATENCY
  5 ms at every sample rate (shown in the corner of the plugin window, and in OBS's filter
  panel). DAWs compensate for it automatically.

Licence: GNU AGPL v3 or later. Source code: see the project repository.
