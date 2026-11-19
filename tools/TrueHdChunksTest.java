package ro.saitatter.audioprobe;
import java.io.*;
import java.security.MessageDigest;
import java.util.Arrays;
public final class TrueHdChunksTest {
  public static void main(String[] a) throws Exception {
    File file=new File(a[0]); byte[] expected=MessageDigest.getInstance("SHA-256").digest(java.nio.file.Files.readAllBytes(file.toPath()));
    for(int n:new int[]{1,16,24}) {
      MessageDigest hash=MessageDigest.getInstance("SHA-256"); long bytes=0,units=0,last=-1;
      try(FrameReader r=new FrameReader(file,FrameReader.Kind.TRUEHD)){
        FrameReader.Unit u;while((u=TrueHdChunks.next(r,n))!=null){if(u.ptsNs<last)throw new AssertionError("PTS regression");last=u.ptsNs;hash.update(u.data);bytes+=u.data.length;units++;}
      }
      if(bytes!=file.length()||!Arrays.equals(expected,hash.digest()))throw new AssertionError("Source changed");
      System.out.printf("group=%d groups=%d bytes=%d lastPts=%.6f s identity=OK%n",n,units,bytes,last/1e9);
    }
  }
}
