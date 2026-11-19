package ro.saitatter.audioprobe;
import java.io.File;
public final class FrameReaderTest {
    public static void main(String[] args) throws Exception {
        File root=new File(args[0]);
        String[] names={"truehd.thd","control.eac3","truehd.iec","truehd.matbe","truehd.matle","pcm.s16le"};
        FrameReader.Kind[] kinds=FrameReader.Kind.values();
        long[] counts=new long[names.length];
        for(int i=0;i<names.length;i++){
            File f=new File(root,names[i]); if(!f.exists())continue;
            long size=0,n=0,prev=-1;
            try(FrameReader r=new FrameReader(f,kinds[i])){
                FrameReader.Unit u;while((u=r.next())!=null){if(u.ptsNs<prev)throw new AssertionError("Timestamp regression");prev=u.ptsNs;n++;size+=u.data.length;}
            }
            if(size!=f.length()||n==0)throw new AssertionError("Read length mismatch: "+f);
            counts[i]=n; System.out.printf("%s units=%d bytes=%d lastPts=%.3f sec%n",names[i],n,size,prev/1e9);
        }
        if(counts[2]==0||counts[2]!=counts[3]||counts[2]!=counts[4])throw new AssertionError("IEC/MAT counts differ");
    }
}
