# Consolidation validation

Host checks executed successfully on the archived source during consolidation:

- FFmpeg synthetic (non-Atmos) 6 s TrueHD stereo and E-AC3 5.1 fixture generation; 299 IEC bursts and corresponding MAT payloads validated.
- FrameReaderTest: 7,200 TrueHD access units, 188 E-AC3 units, 299 IEC/MAT units each and 300 PCM units; complete byte lengths and nondecreasing PTS.
- TrueHdChunksTest: 7,200 groups at 1 AU, 450 at 16 AU, 300 at 24 AU; 105,300 bytes identical by SHA-256 for each size.
- Native scanner: 21 files in the first-stage binary archive, additional stage 2 and stage 3 scans.
- Log analyzer: parsed four reports in reports_v021.zip.
- Python syntax compilation passed for all Python utilities.

The consolidated version was not built as an APK in this environment (Android SDK unavailable), and the new Windows scripts were not executed against a TV. The earlier versions were compiled and tested by the device owner, as documented in the investigation. Host test fixtures cannot validate real Atmos metadata retention or HDMI behavior.
