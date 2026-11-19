package ro.saitatter.audioprobe;
import java.io.*;
/** Concatenate consecutive complete access units; preserve the first AU timestamp. */
final class TrueHdChunks {
    static FrameReader.Unit next(FrameReader reader,int count)throws IOException {
        FrameReader.Unit first=reader.next();
        if(first==null||count==1)return first;
        ByteArrayOutputStream out=new ByteArrayOutputStream(first.data.length*count);
        out.write(first.data);
        for(int i=1;i<count;i++){
            FrameReader.Unit u=reader.next();
            if(u==null)break;
            out.write(u.data);
        }
        return new FrameReader.Unit(out.toByteArray(),first.ptsNs);
    }
}
