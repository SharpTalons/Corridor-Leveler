Corridor Leveler
================

Keeps an audio source at a target level. Quiet passages are lifted, loud ones lowered,
silence and room noise are never boosted, and the output never goes past the ceiling.

WHERE THINGS WERE INSTALLED
  2.x plugin     C:\Program Files\VSTPlugins\Corridor Leveler.dll
  VST3           C:\Program Files\Common Files\VST3\Corridor Leveler.vst3

USING IT IN OBS
  1. Restart OBS.
  2. Right-click your microphone in the Audio Mixer > Filters > + > VST 2.x Plug-in.
  3. Pick "Corridor Leveler" from the list, then press "Open Plug-in Interface".
  Put it after Noise Suppression and before any compressor or EQ.
  If you used an earlier version, remove its old "Corridor Leveler" filter from your sources;
  this installer takes that filter out of OBS.

USING IT IN A DAW
  Rescan plugins, then insert Corridor Leveler on the track (VST3 or 2.x).

GOOD STARTING POINT FOR A STREAM VOICE
  Target -20 dB RMS, Threshold -48 dB RMS, Response 50, Range 12 dB, Ceiling -1 dBFS.
  Set your sound card so your loudest shouts peak around -10 to -6 dBFS. Corridor can't undo
  clipping that already happened before OBS.

LATENCY
  5 ms at every sample rate (shown in the corner of the plugin window). DAWs compensate for it
  automatically.

Licence: GNU AGPL v3 or later. Source code: see the project repository.
