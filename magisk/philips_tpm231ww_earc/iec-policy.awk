BEGIN {
    name = "direct_iec61937 output"
    profile = "            <profile name=\"\" format=\"AUDIO_FORMAT_IEC61937\" samplingRates=\"192000\""
    channels = "                     channelMasks=\"AUDIO_CHANNEL_OUT_7POINT1\"/>"
}
/^    <\/mixPorts>$/ {
    print "        <mixPort name=\"" name "\" role=\"source\""
    print "                 flags=\"AUDIO_OUTPUT_FLAG_DIRECT\" maxOpenCount=\"1\" maxActiveCount=\"1\">"
    print profile
    print channels
    print "        </mixPort>"
    ++port
}
/<devicePort tagName="HDMI EARC" type="AUDIO_DEVICE_OUT_HDMI_EARC" role="sink">/ {
    earc = 1
}
earc && /<\/devicePort>/ {
    print profile
    print channels
    earc = 0
    ++device
}
/<route type="mix" sink="HDMI EARC"/ {
    in_route = 1
}
in_route && /sources="[^"]+"\/>/ {
    sub(/"\/>/, "," name "\"/>")
    in_route = 0
    ++route
}
{ print }
END { if (port != 1 || device != 1 || route != 1) exit 1 }
