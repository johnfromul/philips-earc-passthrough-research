# Offline IEC61937 client experiment, V670

## APEX layout resolved

Owner-supplied tcore_layout.json identifies the stock system_ext/apex/com.tcl.tcore.apex and its extracted payload/lib/libaudioclient.so as a symbolic link to /system/lib/libaudioclient.so. apex-info-list.xml confirms the active com.tcl.tcore version 653670001 is factory=true, active=true, with modulePath and preinstalledModulePath /system_ext/apex/com.tcl.tcore.apex. The earlier missing result excluded symlinks. Combined with the identical live library hashes, this resolves the duplicate-client question for this file. No APEX rebuild needed for this client experiment; changing other TCL policy/flinger components may still require separate work. Do not infer whole-system cross-region compatibility.

## First local prototype, not deployable

tools/patch_iec_client.py is a deterministic, exact-stock-hash-gated generator. It leaves the original file unchanged, rejects other builds and previously patched input, maps ELF VAs to executable file offsets using PT_LOAD, validates original instruction bytes, and emits a separate file and report. No root, no device commands and no flashing/signing code. It does not impersonate a process or force a capability true: the intended paths proceed with normal policy/format checks. It removes the four known app-identity-based IEC61937 client format filters on the owner's firmware copy for offline research.

| Purpose | Thumb VA | Original LE bytes | New LE bytes | Normal path VA |
|---|---|---|---|---|
| Offload query | 0x5c636 | 40f0ca80 | cae000bf | 0x5c7ce |
| Direct query | 0x67f58 | 40f0d280 | d2e000bf | 0x68100 |
| Direct profiles | 0x68932 | acb1 | 1ee0 | 0x68972 |
| AudioTrack set | 0x6d862 | 73d1 | 73e0 | 0x6d94c |

The original first two branches are conditional bne.w following format comparisons with 0x0d000000; replacements are b plus nop of equal total width. Direct profiles branches to the common cleanup after the identity test, preserving returned policy profiles rather than removing IEC. AudioTrack::set branches to existing generic format/flag validation, preserving r9 containing original encoding. This is a static control-flow experiment, not proven correct runtime behavior.

Verified offline on actual previously extracted stock client: SHA-256 dff373698a6eb20316cfa26031529c9c2b5d8edf523174a22668fd0dbc5dd4a9, size 702872. Output SHA-256 4f3aabd50c78faa3f19acc0a6078691161dea3c14fda92772247b54875a78d25; identical size, 11 bytes differ across 4 sites (12 bytes covered). llvm-objdump confirms all 4 actual branch destinations. Host assertions passed for deterministic output, rejection of truncated/mutated/already-modified input, and decoded Thumb immediate targets. The actual ARM code has not been executed. The original ELF note/build ID is preserved, which is not an authenticity signature. Do not install the experimental file.

From the Windows repository root, using the already extracted WSL workspace reported by the owner:

    wsl -e bash -lc 'python3 tools/patch_iec_client.py ~/tcl_stock_040p83wi/system/system/lib/libaudioclient.so'
    wsl -e bash -lc 'python3 tools/test_iec_client_patch.py ~/tcl_stock_040p83wi/system/system/lib/libaudioclient.so'

The first command fails if the output directory already exists. Generated local file and report are under private/iec_client_experiment; never commit or redistribute modified proprietary binary. Only report and original generator/test source are intended for sharing.

## Remaining blocking work

Client prototype alone will not create a valid eARC IEC61937 profile or a working transport. HAL's format eligibility and EarcOutput classifier currently reject IEC input; forcing a format alias could misroute compressed bursts as PCM. Need evidence-based HAL design: determine input contract, valid burst classification and digital output signaling, negotiate receiver format capabilities and restrict any experimental direct profile to eARC. No force-success or PCM masquerading. The locked AVB bootloader, exact GB AU04 recovery, snapshot state, and supported development/deployment route remain unresolved. No flashable image has been generated. Do not apply these changes to a live unit.