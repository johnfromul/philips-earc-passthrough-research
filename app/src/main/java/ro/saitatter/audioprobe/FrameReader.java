package ro.saitatter.audioprobe;

import java.io.*;
import java.util.Arrays;

/** Read prepared elementary access units or fixed-size carrier bursts, never decode. */
final class FrameReader implements Closeable {
    enum Kind { TRUEHD, EAC3, IEC, MAT_BE, MAT_LE, PCM }
    static final int IEC_BYTES = 61440, MAT_BYTES = 61424;
    final Kind kind;
    final DataInputStream stream;
    long ptsNs = 0;
    private int lastTrueHdTiming = -1;
    private long samples = 0;
    FrameReader(File file, Kind kind) throws IOException {
        this.kind = kind;
        stream = new DataInputStream(new BufferedInputStream(new FileInputStream(file)));
    }
    static final class Unit {
        final byte[] data; final long ptsNs;
        Unit(byte[] data, long ptsNs) { this.data=data; this.ptsNs=ptsNs; }
    }
    private byte[] readExact(int n) throws IOException {
        byte[] result = new byte[n];
        int first=stream.read();
        if (first<0) return null;
        result[0]=(byte)first;
        stream.readFully(result,1,n-1);
        return result;
    }
    Unit next() throws IOException {
        switch (kind) {
            case TRUEHD: {
                byte[] h=readExact(4); if (h==null) return null;
                int length=(((h[0]&15)<<8)|(h[1]&255))*2;
                if (length<8 || length>8190) throw new IOException("Invalid TrueHD AU size: "+length);
                byte[] data=Arrays.copyOf(h,length); stream.readFully(data,4,length-4);
                int timing=((h[2]&255)<<8)|(h[3]&255);
                if (lastTrueHdTiming>=0) {
                    int delta=(timing-lastTrueHdTiming)&65535;
                    // Prepared samples are TrueHD 48 kHz; tolerate duplicate timing, not discontinuities.
                    samples+=delta>0 && delta<=1920?delta:40;
                }
                lastTrueHdTiming=timing;
                return new Unit(data,samples*1000000000L/48000);
            }
            case EAC3: {
                byte[] h=readExact(5); if (h==null) return null;
                if ((h[0]&255)!=0x0b || (h[1]&255)!=0x77) throw new IOException("Invalid E-AC3 sync");
                int length=((((h[2]&7)<<8)|(h[3]&255))+1)*2;
                int fscod=(h[4]&0xc0)>>6;
                if (length<6 || fscod!=0) throw new IOException("Only 48 kHz E-AC3 is supported");
                byte[] data=Arrays.copyOf(h,length); stream.readFully(data,5,length-5);
                int blocks=new int[]{1,2,3,6}[(h[4]&0x30)>>4];
                long time=samples*1000000000L/48000;
                // Dependent substreams accompany an independent substream, no EOS or HW sync test here.
                if (((h[2]&0xc0)>>6)!=1) samples+=blocks*256L;
                return new Unit(data,time);
            }
            case IEC: {
                byte[] data=readExact(IEC_BYTES); if (data==null) return null;
                if ((data[0]&255)!=0x72 || (data[1]&255)!=0xf8 || (data[2]&255)!=0x1f || (data[3]&255)!=0x4e || (data[4]&31)!=0x16)
                    throw new IOException("Expected little-endian TrueHD IEC61937 burst");
                return fixed(data);
            }
            case MAT_BE: case MAT_LE: {
                byte[] data=readExact(MAT_BYTES); if (data==null) return null;
                int a=kind==Kind.MAT_BE?0x07:0x9e, b=kind==Kind.MAT_BE?0x9e:0x07;
                if ((data[0]&255)!=a || (data[1]&255)!=b) throw new IOException("Unexpected MAT byte order or header");
                return fixed(data);
            }
            case PCM: {
                byte[] data=new byte[3840]; // 20 ms, 48 kHz stereo signed 16-bit
                int got=0; while (got<data.length) { int n=stream.read(data,got,data.length-got); if(n<0) break; got+=n; }
                data=Arrays.copyOf(data,got);
                if (data.length==0) return null;
                if (data.length%4!=0) throw new IOException("Incomplete PCM frame");
                return fixed(data);
            }
            default: throw new AssertionError(kind);
        }
    }
    private Unit fixed(byte[] data) { long t=ptsNs; ptsNs+=20000000; return new Unit(data,t); }
    @Override public void close() throws IOException { stream.close(); }
}
