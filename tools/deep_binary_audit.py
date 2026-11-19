import struct
import sys
import capstone

def analyze_libmi3(path):
    print("=" * 60)
    print("ANALYZING libmi3_audio_hal.so:", path)
    with open(path, "rb") as f:
        data = f.read()

    # Find string offsets
    strings_to_find = [b"MatMlpOnly", b"MatTrueHDOnly", b"MatStreamType", b"MatAtmosStream", b"DisableAtmos", b"MAT_HDMI_bypass"]
    for s in strings_to_find:
        idx = 0
        while True:
            idx = data.find(s, idx)
            if idx == -1:
                break
            print(f"String '{s.decode()}' found at file offset 0x{idx:x}")
            idx += len(s)

    # Disassemble around known functions using Capstone ARM / Thumb
    md_thumb = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
    md_thumb.detail = True

    # Check _MI_LAPAF_FILTER_DecoderCommon_SetMatMlpOnly at 0x16a9d8
    # Let's inspect bytes at 0x16a9d8
    target_addr = 0x16a9d8
    print(f"\nDisassembly of _MI_LAPAF_FILTER_DecoderCommon_SetMatMlpOnly (0x{target_addr:x}):")
    code = data[target_addr:target_addr + 48]
    for ins in md_thumb.disasm(code, target_addr):
        print(f"  0x{ins.address:08x}: {ins.mnemonic:8s} {ins.op_str}")

    # Search for all instructions accessing [rX, #0x348] in Thumb mode
    print("\nScanning for all LDR/STR instructions referencing offset 0x348 across code...")
    # Typically in .text section. Let's scan from 0x50000 to 0x250000 in chunks
    step = 0x10000
    for chunk_start in range(0x40000, min(len(data), 0x220000), step):
        code_chunk = data[chunk_start:chunk_start + step]
        for ins in md_thumb.disasm(code_chunk, chunk_start):
            if "#0x348" in ins.op_str or "#840" in ins.op_str:
                print(f"  0x{ins.address:08x}: {ins.mnemonic:8s} {ins.op_str}")

def analyze_extractor(path):
    print("\n" + "=" * 60)
    print("ANALYZING libmtkmkvextractor.so:", path)
    with open(path, "rb") as f:
        data = f.read()

    # Search for truehd strings
    for s in [b"truehd", b"TrueHD", b"TRUEHD", b"support.truehd", b"mlp", b"MLP", b"audio/vnd.dolby.mlp"]:
        idx = 0
        while True:
            idx = data.find(s, idx)
            if idx == -1:
                break
            # print surrounding context
            start = max(0, idx - 20)
            end = min(len(data), idx + len(s) + 40)
            snippet = data[start:end]
            clean = "".join(chr(b) if 32 <= b < 127 else "." for b in snippet)
            print(f"Match '{s.decode()}': offset 0x{idx:x} -> {clean}")
            idx += len(s)

def analyze_merak(path):
    print("\n" + "=" * 60)
    print("ANALYZING audio.primary.merak.so:", path)
    with open(path, "rb") as f:
        data = f.read()

    md_thumb = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
    md_thumb.detail = True

    # Disassemble utils_isPassthroughSupported at 0x70bd8
    print("\nDisassembly of utils_isPassthroughSupported (0x70bd8):")
    code = data[0x70bd8:0x70bd8 + 86]
    for ins in md_thumb.disasm(code, 0x70bd8):
        print(f"  0x{ins.address:08x}: {ins.mnemonic:8s} {ins.op_str}")

    # Disassemble utils_earc_set_decoder_format at 0x71b88
    print("\nDisassembly of utils_earc_set_decoder_format snippet around format checks (0x71c00-0x71c80):")
    code = data[0x71c00:0x71c80]
    for ins in md_thumb.disasm(code, 0x71c00):
        print(f"  0x{ins.address:08x}: {ins.mnemonic:8s} {ins.op_str}")

    # Disassemble __apply_iec_config_by_stream at 0x73570
    print("\nDisassembly of __apply_iec_config_by_stream entry (0x73570-0x73600):")
    code = data[0x73570:0x73600]
    for ins in md_thumb.disasm(code, 0x73570):
        print(f"  0x{ins.address:08x}: {ins.mnemonic:8s} {ins.op_str}")

if __name__ == "__main__":
    analyze_libmi3("private/verify_20261119/libmi3_audio_hal.so")
    analyze_extractor("private/root_dumps/stage4/libmtkmkvextractor.so")
    analyze_merak("private/verify_20261119/audio.primary.merak.so")
