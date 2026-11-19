// hook_mat_mlp.js
// Frida instrumentation script for testing MatMlpOnly=0 hypothesis on TCL MediaTek Audio HAL.
// Target process: vendor.mediatek.hardware.audio.service
// Target module: libmi3_audio_hal.so

console.log("[*] Initializing MatMlpOnly instrumentation script...");

const moduleName = "libmi3_audio_hal.so";
const targetSymbol = "_MI_LAPAF_FILTER_DecoderCommon_SetMatMlpOnly";

// Wait for module if not yet loaded
const m = Process.findModuleByName(moduleName);
if (m) {
    hook(m);
} else {
    console.log(`[*] Waiting for ${moduleName} to be loaded...`);
    const interval = setInterval(() => {
        const mod = Process.findModuleByName(moduleName);
        if (mod) {
            clearInterval(interval);
            hook(mod);
        }
    }, 500);
}

function hook(mod) {
    console.log(`[+] Found ${mod.name} at base: ${mod.base}`);

    let targetAddr = mod.findExportByName(targetSymbol);
    if (!targetAddr) {
        // Fallback to static offset if stripped (offset 0x16a9d8 Thumb)
        const staticOffset = 0x16a9d8;
        targetAddr = mod.base.add(staticOffset);
        console.log(`[!] Export not found; using static Thumb offset 0x${staticOffset.toString(16)} -> ${targetAddr}`);
    } else {
        console.log(`[+] Found exported symbol ${targetSymbol} at: ${targetAddr}`);
    }

    try {
        Interceptor.attach(targetAddr, {
            onEnter: function (args) {
                // args[0] = filter state pointer
                // args[1] = pointer to uint32 parameter value (MatMlpOnly)
                const pVal = args[1];
                if (!pVal.isNull()) {
                    try {
                        const currentVal = pVal.readU32();
                        console.log(`[>>] ${targetSymbol} invoked with MatMlpOnly = ${currentVal}`);
                        if (currentVal === 1) {
                            console.log(`[**] OVERRIDE: Forcing MatMlpOnly: 1 -> 0 (Enabling Atmos metadata)`);
                            pVal.writeU32(0);
                        }
                    } catch (e) {
                        console.log(`[-] Error reading/writing value pointer: ${e.message}`);
                    }
                }
            },
            onLeave: function (retval) {
                // Return value untouched
            }
        });
        console.log(`[+] Successfully attached hook to ${targetSymbol}!`);
    } catch (err) {
        console.error(`[-] Failed to attach Interceptor: ${err.message}`);
    }
}
