package ro.saitatter.audioprobe;

import android.media.AudioAttributes;
import android.media.AudioFormat;

/** MAT value 19 exists in Android 14 framework as a non-public/system encoding.
 * Public SDK acceptance is not assumed; failures are diagnostic results. */
enum ProbeMode {
    PCM("PCM stereo control",FrameReader.Kind.PCM,"pcm.s16le",2,48000,AudioFormat.CHANNEL_OUT_STEREO,false,false),
    EAC3("E-AC3 / JOC control (RAW)",FrameReader.Kind.EAC3,"control.eac3",6,48000,AudioFormat.CHANNEL_OUT_5POINT1,false,false),
    TRUEHD_RAW("TrueHD RAW 48k 7.1 (offload not requested)",FrameReader.Kind.TRUEHD,"truehd.thd",14,48000,AudioFormat.CHANNEL_OUT_7POINT1_SURROUND,false,false),
    TRUEHD_OFFLOAD("TrueHD explicit offload 48k 7.1",FrameReader.Kind.TRUEHD,"truehd.thd",14,48000,AudioFormat.CHANNEL_OUT_7POINT1_SURROUND,true,false),
    TRUEHD_SYNC("TrueHD HW AV Sync 48k 7.1",FrameReader.Kind.TRUEHD,"truehd.thd",14,48000,AudioFormat.CHANNEL_OUT_7POINT1_SURROUND,false,true),
    IEC("IEC61937 TrueHD HBR 192k 7.1",FrameReader.Kind.IEC,"truehd.iec",13,192000,AudioFormat.CHANNEL_OUT_7POINT1_SURROUND,false,false),
    IEC_SYNC("IEC61937 TrueHD HBR + HW AV Sync",FrameReader.Kind.IEC,"truehd.iec",13,192000,AudioFormat.CHANNEL_OUT_7POINT1_SURROUND,false,true),
    MAT_BE("MAT RAW BE payload 48k 7.1 (experimental)",FrameReader.Kind.MAT_BE,"truehd.matbe",19,48000,AudioFormat.CHANNEL_OUT_7POINT1_SURROUND,false,false),
    MAT_LE("MAT RAW LE payload 48k 7.1 (experimental)",FrameReader.Kind.MAT_LE,"truehd.matle",19,48000,AudioFormat.CHANNEL_OUT_7POINT1_SURROUND,false,false),
    MAT_192("MAT RAW BE payload 192k 7.1 (experimental)",FrameReader.Kind.MAT_BE,"truehd.matbe",19,192000,AudioFormat.CHANNEL_OUT_7POINT1_SURROUND,false,false),
    MAT_SYNC("MAT RAW BE + HW AV Sync (experimental)",FrameReader.Kind.MAT_BE,"truehd.matbe",19,48000,AudioFormat.CHANNEL_OUT_7POINT1_SURROUND,false,true);
    final String label, file; final FrameReader.Kind kind; final int encoding, rate, mask; final boolean offload, sync;
    ProbeMode(String label, FrameReader.Kind kind, String file, int encoding, int rate, int mask, boolean offload, boolean sync) {
        this.label=label; this.kind=kind; this.file=file; this.encoding=encoding; this.rate=rate; this.mask=mask; this.offload=offload; this.sync=sync;
    }
    AudioFormat format() { return new AudioFormat.Builder().setEncoding(encoding).setSampleRate(rate).setChannelMask(mask).build(); }
    AudioAttributes attrs() {
        AudioAttributes.Builder b=new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MOVIE);
        if(sync) b.setFlags(AudioAttributes.FLAG_HW_AV_SYNC);
        return b.build();
    }
    @Override public String toString(){ return label; }
}
