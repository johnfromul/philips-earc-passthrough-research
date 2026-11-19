package ro.saitatter.audioprobe;

import android.app.Activity;
import android.app.AlertDialog;
import android.media.*;
import android.os.*;
import android.util.Log;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.nio.ByteBuffer;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

/** Public-API, on-demand A/V hardware tunneling probe. No fallback to non-tunneled video. */
public final class TunnelActivity extends Activity implements SurfaceHolder.Callback {
    private static final String TAG="AudioTunnelProbe";
    private static final long RUN_MS=25000, STALL_MS=8000;
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final Handler ui=new Handler(Looper.getMainLooper());
    private final Object logLock=new Object();
    private volatile boolean cancel;
    private boolean busy, surfaceReady;
    private File dir, report;
    private SurfaceView video;
    private TextView output, state;
    private ScrollView scroll;
    private Spinner modes, observation, picture;
    private Button start, stop, record, back, inspect;
    private Mode lastMode;
    private int lastSession;

    private enum Mode {
        PCM("1. PCM 5.1 tunnel control",2,AudioFormat.CHANNEL_OUT_5POINT1,"tunnel_pcm51.s16le",true,false),
        TRUEHD("2. TrueHD 7.1 tunnel (audio first)",14,AudioFormat.CHANNEL_OUT_7POINT1_SURROUND,"truehd.thd",false,false),
        TRUEHD_CODEC_FIRST("3. TrueHD 7.1 tunnel (video first)",14,AudioFormat.CHANNEL_OUT_7POINT1_SURROUND,"truehd.thd",false,true),
        EAC3("4. E-AC3 5.1 tunnel (optional)",6,AudioFormat.CHANNEL_OUT_5POINT1,"control.eac3",false,false),
        JOC("5. E-AC3 JOC tunnel (optional Atmos sample)",18,AudioFormat.CHANNEL_OUT_5POINT1,"control.eac3",false,false),
        TRUEHD_GROUP16("6. TrueHD 7.1 tunnel — 16 AU per write",14,AudioFormat.CHANNEL_OUT_7POINT1_SURROUND,"truehd.thd",false,false),
        TRUEHD_GROUP24("7. TrueHD 7.1 tunnel — 24 AU per write",14,AudioFormat.CHANNEL_OUT_7POINT1_SURROUND,"truehd.thd",false,false);
        final String label,file; final int encoding,mask; final boolean pcm,codecFirst;
        Mode(String label,int encoding,int mask,String file,boolean pcm,boolean codecFirst){this.label=label;this.encoding=encoding;this.mask=mask;this.file=file;this.pcm=pcm;this.codecFirst=codecFirst;}
        int groupSize(){return this==TRUEHD_GROUP16?16:this==TRUEHD_GROUP24?24:1;}
        public String toString(){return label;}
        AudioFormat format(){return new AudioFormat.Builder().setEncoding(encoding).setSampleRate(48000).setChannelMask(mask).build();}
    }
    private static AudioAttributes attrs(){return new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MOVIE).setFlags(AudioAttributes.FLAG_HW_AV_SYNC).build();}
    @Override public void onCreate(Bundle b){
        super.onCreate(b); getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setVolumeControlStream(AudioManager.STREAM_MUSIC);
        dir=getExternalFilesDir(null); if(dir==null)dir=getFilesDir(); report=new File(dir,"tunnel.log");
        LinearLayout root=new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL); root.setPadding(20,12,20,12);
        state=new TextView(this); state.setTextSize(20); state.setText("Full A/V tunnel v0.2.1 — lower soundbar volume"); root.addView(state);
        modes=new Spinner(this); bind(modes,Mode.values()); root.addView(modes);
        LinearLayout controls=new LinearLayout(this); root.addView(controls);
        inspect=button(controls,"Inspect",()->job(()->listCodecs()));
        start=button(controls,"Start 25 sec",()->confirm());
        stop=button(controls,"STOP",()->{cancel=true;log("USER STOP");});
        back=button(controls,"Back",()->finish());
        LinearLayout content=new LinearLayout(this); root.addView(content,new LinearLayout.LayoutParams(-1,0,1));
        video=new SurfaceView(this); video.getHolder().addCallback(this); content.addView(video,new LinearLayout.LayoutParams(0,-1,1));
        scroll=new ScrollView(this); scroll.setFocusable(false); output=new TextView(this); output.setTypeface(android.graphics.Typeface.MONOSPACE); output.setTextSize(12); scroll.addView(output); content.addView(scroll,new LinearLayout.LayoutParams(0,-1,1));
        LinearLayout results=new LinearLayout(this); root.addView(results);
        observation=new Spinner(this); bind(observation,new String[]{"Audio result","Atmos + normal sound","Dolby Surround + normal sound","PCM + normal sound","Other normal sound","No sound","Noise / beep","Failed"}); results.addView(observation,new LinearLayout.LayoutParams(0,-2,2));
        picture=new Spinner(this); bind(picture,new String[]{"Video result","Moving video","Frozen picture","Black screen","Not sure"}); results.addView(picture,new LinearLayout.LayoutParams(0,-2,2));
        record=button(results,"Record result",()->{if(lastMode!=null)log("OBSERVED mode="+lastMode.name()+" session="+lastSession+" audio="+observation.getSelectedItem()+" video="+picture.getSelectedItem());});
        setContentView(root); update(); inspect.requestFocus(); log("START v0.2.1 pid="+android.os.Process.myPid()+" fingerprint="+Build.FINGERPRINT+" dir="+dir);
    }
    private <T> void bind(Spinner s,T[] items){ArrayAdapter<T> a=new ArrayAdapter<>(this,android.R.layout.simple_spinner_item,items);a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);s.setAdapter(a);}
    private Button button(LinearLayout r,String t,Runnable click){Button b=new Button(this);b.setText(t);b.setAllCaps(false);b.setOnClickListener(v->click.run());r.addView(b,new LinearLayout.LayoutParams(0,-2,1));return b;}
    private void update(){start.setEnabled(!busy&&surfaceReady);inspect.setEnabled(!busy);stop.setEnabled(busy);back.setEnabled(!busy);modes.setEnabled(!busy);record.setEnabled(!busy&&lastMode!=null);}
    private void log(String s){String l=new SimpleDateFormat("HH:mm:ss.SSS",Locale.US).format(new Date())+" "+s;Log.i(TAG,s);synchronized(logLock){try(FileWriter w=new FileWriter(report,true)){w.write(l+"\n");}catch(IOException e){Log.e(TAG,"report",e);}}ui.post(()->{if(isDestroyed()||output==null)return;if(output.length()>20000)output.setText(output.getText().subSequence(output.length()-12000,output.length())); output.append(l+"\n");scroll.post(()->scroll.scrollTo(0,output.getBottom()));});}
    private interface Job {void run()throws Exception;}
    private void job(Job j){if(busy)return;busy=true;cancel=false;update();stop.requestFocus();worker.execute(()->{try{j.run();}catch(Exception e){log("FAIL "+e.getClass().getSimpleName()+": "+e.getMessage());Log.e(TAG,"probe failed",e);}finally{ui.post(()->{if(isDestroyed())return;busy=false;state.setText("Done — select audio and video result, then Record result");update();if(start.isEnabled())start.requestFocus();});}});}
    private void confirm(){Mode m=(Mode)modes.getSelectedItem();Surface surface=video.getHolder().getSurface();if(!surfaceReady||!surface.isValid())return;new AlertDialog.Builder(this).setTitle(m.label).setMessage("Lower the physical soundbar volume. Stop immediately if noise occurs. Video should move on the left; record what the soundbar displays.").setNegativeButton("Cancel",null).setPositiveButton("Start",(d,w)->{lastMode=m;lastSession=0;observation.setSelection(0);picture.setSelection(0);state.setText("Running "+m.label);job(()->run(m,surface));}).show();}
    @Override public void surfaceCreated(SurfaceHolder h){surfaceReady=true;update();}
    @Override public void surfaceChanged(SurfaceHolder h,int f,int w,int v){surfaceReady=true;update();}
    @Override public void surfaceDestroyed(SurfaceHolder h){surfaceReady=false;cancel=true;update();}
    @Override protected void onStop(){cancel=true;super.onStop();}
    @Override protected void onDestroy(){cancel=true;worker.shutdownNow();super.onDestroy();}

    private void listCodecs(){
        for(MediaCodecInfo i:new MediaCodecList(MediaCodecList.ALL_CODECS).getCodecInfos()){
            if(i.isEncoder())continue;
            for(String mime:i.getSupportedTypes())if(mime.equalsIgnoreCase("video/avc")||mime.equalsIgnoreCase("video/hevc"))try{
                MediaCodecInfo.CodecCapabilities c=i.getCapabilitiesForType(mime);
                log("CODEC "+i.getName()+" mime="+mime+" hardware="+i.isHardwareAccelerated()+" tunnel="+c.isFeatureSupported(MediaCodecInfo.CodecCapabilities.FEATURE_TunneledPlayback)+" secureRequired="+c.isFeatureRequired(MediaCodecInfo.CodecCapabilities.FEATURE_SecurePlayback));
            }catch(Exception e){log("CODEC inspect failed "+i.getName()+": "+e);}
        }
        for(Mode m:Mode.values())try{log("AUDIO_QUERY "+m.name()+" supported="+AudioTrack.isDirectPlaybackSupported(m.format(),attrs())+" bits=0x"+Integer.toHexString(AudioManager.getDirectPlaybackSupport(m.format(),attrs()))+" file="+new File(dir,m.file).length());}catch(Exception e){log("AUDIO_QUERY failed "+m+": "+e);}
        log("VIDEO_FILE bytes="+new File(dir,"tunnel_video.mp4").length());
    }
    private String chooseCodec(MediaFormat format){
        String mime=format.getString(MediaFormat.KEY_MIME); String candidate=null;
        for(MediaCodecInfo i:new MediaCodecList(MediaCodecList.ALL_CODECS).getCodecInfos()){
            if(i.isEncoder()||!i.isHardwareAccelerated())continue;
            try{MediaCodecInfo.CodecCapabilities c=i.getCapabilitiesForType(mime);
                if(!c.isFeatureSupported(MediaCodecInfo.CodecCapabilities.FEATURE_TunneledPlayback)||c.isFeatureRequired(MediaCodecInfo.CodecCapabilities.FEATURE_SecurePlayback))continue;
                boolean supported=c.isFormatSupported(format); log("TUNNEL_CANDIDATE name="+i.getName()+" formatSupported="+supported);
                if(supported&&candidate==null)candidate=i.getName();
            }catch(IllegalArgumentException ignored){}
        }
        if(candidate==null)throw new UnsupportedOperationException("No advertised non-secure hardware tunneled decoder supports "+format);
        return candidate;
    }
    private AudioTrack audio(Mode m,int session){
        int min=AudioTrack.getMinBufferSize(48000,m.mask,m.encoding);int size=Math.max(min,m.pcm?115200:245760);int alignment=m.pcm?12:16;size=((size+alignment-1)/alignment)*alignment;
        log("AUDIO_BUILD session="+session+" enc="+m.encoding+" rate=48000 mask=0x"+Integer.toHexString(m.mask)+" buffer="+size+" min="+min+" HW_AV_SYNC=true offload=false");
        AudioTrack a=new AudioTrack.Builder().setAudioAttributes(attrs()).setAudioFormat(m.format()).setTransferMode(AudioTrack.MODE_STREAM).setSessionId(session).setBufferSizeInBytes(size).setOffloadedPlayback(false).build();
        if(a.getState()!=AudioTrack.STATE_INITIALIZED||a.getAudioSessionId()!=session){a.release();throw new IllegalStateException("AudioTrack session/state mismatch");}
        log("AUDIO_BUILT session="+a.getAudioSessionId()+" actualEncoding="+a.getAudioFormat()+" channels="+a.getChannelCount()+" offloaded="+a.isOffloadedPlayback()+" bufferFrames="+a.getBufferSizeInFrames());return a;
    }
    private static final class VideoFeed {
        long baseUs, frames, lastPtsUs=-1; boolean done;
        VideoFeed(long base){baseUs=base;}
        boolean pump(MediaCodec c,MediaExtractor x,long limitUs)throws IOException{
            if(done)return false;long pts=x.getSampleTime();if(pts<0||pts-baseUs>=RUN_MS*1000){done=true;return false;}
            if(pts-baseUs>limitUs)return false;
            int id=c.dequeueInputBuffer(0);if(id<0)return false;
            ByteBuffer b=c.getInputBuffer(id);if(b==null)throw new IOException("Missing video input buffer");b.clear();int n=x.readSampleData(b,0);if(n<0)throw new EOFException("Video sample vanished");
            long relative=Math.max(0,pts-baseUs);c.queueInputBuffer(id,0,n,relative,0);x.advance();frames++;lastPtsUs=relative;return true;
        }
    }
    private static final class AudioFeed implements Closeable {
        final FrameReader compressed; final DataInputStream pcm; final int groupSize; long pcmSamples;
        AudioFeed(File f,Mode m)throws IOException{groupSize=m.groupSize();if(m.pcm){pcm=new DataInputStream(new BufferedInputStream(new FileInputStream(f)));compressed=null;}else{pcm=null;compressed=new FrameReader(f,m.encoding==14?FrameReader.Kind.TRUEHD:FrameReader.Kind.EAC3);}}
        FrameReader.Unit next()throws IOException{if(compressed!=null)return TrueHdChunks.next(compressed,groupSize);byte[] data=new byte[11520];int n=pcm.read();if(n<0)return null;data[0]=(byte)n;pcm.readFully(data,1,data.length-1);long pts=pcmSamples*1000000000L/48000;pcmSamples+=960;return new FrameReader.Unit(data,pts);}
        public void close()throws IOException{if(compressed!=null)compressed.close();else pcm.close();}
    }
    private String audioStatus(AudioTrack a){AudioTimestamp t=new AudioTimestamp();boolean ok=a.getTimestamp(t);AudioDeviceInfo d=a.getRoutedDevice();return "head="+Integer.toUnsignedLong(a.getPlaybackHeadPosition())+" timestamp="+(ok?t.framePosition+"/"+t.nanoTime:"unavailable")+" route="+(d==null?"none":d.getId()+"/"+d.getType()+"/"+d.getProductName());}
    private void run(Mode m,Surface surface)throws Exception{
        File vf=new File(dir,"tunnel_video.mp4"), af=new File(dir,m.file);if(!vf.isFile()||!af.isFile())throw new FileNotFoundException("Missing sample: "+(!vf.isFile()?vf:af));
        int session=((AudioManager)getSystemService(AUDIO_SERVICE)).generateAudioSessionId();if(session<=0)throw new IOException("generateAudioSessionId failed: "+session);lastSession=session;
        log("=== TUNNEL TEST "+m.name()+" session="+session+" codecFirst="+m.codecFirst+" groupSize="+m.groupSize()+" ===");
        AudioTrack a=null;MediaCodec c=null;boolean started=false;MediaExtractor x=new MediaExtractor();AtomicLong rendered=new AtomicLong(),lastRendered=new AtomicLong(-1);AtomicBoolean firstReady=new AtomicBoolean();
        try(AudioFeed feed=new AudioFeed(af,m)){
            x.setDataSource(vf.getAbsolutePath());MediaFormat fmt=null;
            for(int i=0;i<x.getTrackCount();i++){MediaFormat f=x.getTrackFormat(i);if("video/avc".equals(f.getString(MediaFormat.KEY_MIME))){fmt=f;x.selectTrack(i);break;}}
            if(fmt==null||x.getSampleTime()<0)throw new IOException("No AVC video track");
            VideoFeed videoFeed=new VideoFeed(x.getSampleTime());
            fmt.setFeatureEnabled(MediaCodecInfo.CodecCapabilities.FEATURE_TunneledPlayback,true);
            String decoder=chooseCodec(fmt);fmt.setInteger(MediaFormat.KEY_AUDIO_SESSION_ID,session);
            if(!m.codecFirst)a=audio(m,session);
            c=MediaCodec.createByCodecName(decoder); log("VIDEO_CONFIGURE decoder="+decoder+" session="+session+" format="+fmt);
            c.configure(fmt,surface,null,0);
            c.setOnFirstTunnelFrameReadyListener(ui,codec->{firstReady.set(true);log("FIRST_TUNNEL_FRAME_READY session="+session);});
            c.setOnFrameRenderedListener((codec,pts,ns)->{long n=rendered.incrementAndGet();lastRendered.set(pts);if(n==1||n%30==0)log("VIDEO_RENDERED session="+session+" callbacks="+n+" ptsUs="+pts);},ui);
            if(m.codecFirst)a=audio(m,session);
            c.start();started=true;log("VIDEO_STARTED decoder="+decoder+" session="+session+" inputFormat="+c.getInputFormat());
            try{Bundle params=new Bundle();params.putInt(MediaCodec.PARAMETER_KEY_TUNNEL_PEEK,0);c.setParameters(params);}catch(Exception e){log("TUNNEL_PEEK unsupported: "+e);}
            FrameReader.Unit unit=feed.next();if(unit==null)throw new EOFException("Empty audio sample");
            ByteBuffer ab=ByteBuffer.allocateDirect(262144);ab.put(unit.data).flip();
            // Prime video and timestamped audio while paused. Never wait indefinitely for frames.
            long bytes=0,units=0,primeUntil=SystemClock.elapsedRealtime()+1200;
            while(!cancel&&SystemClock.elapsedRealtime()<primeUntil&&videoFeed.frames<6){if(!videoFeed.pump(c,x,250000))SystemClock.sleep(3);}
            if(!cancel){int n=a.write(ab,ab.remaining(),AudioTrack.WRITE_NON_BLOCKING,unit.ptsNs);if(n<0)throw new IOException("Prime AudioTrack.write="+n);bytes+=n;}
            if(cancel)return;
            log("TUNNEL_PRIMED videoQueued="+videoFeed.frames+" audioBytes="+bytes+" audioPtsNs="+unit.ptsNs);
            a.play();long start=SystemClock.elapsedRealtime(),lastProgress=start,lastReport=start-1000;log("TUNNEL_PLAY session="+session+" mode="+m.name()+" "+audioStatus(a));
            MediaCodec.BufferInfo out=new MediaCodec.BufferInfo();long unexpectedOutputs=0;boolean pollOutput=true;
            while(!cancel&&SystemClock.elapsedRealtime()-start<RUN_MS){
                long now=SystemClock.elapsedRealtime(), elapsed=now-start;boolean progress=false;
                // Interleave both inputs; never block audio feeding waiting for video buffers.
                for(int i=0;i<6;i++){if(!videoFeed.pump(c,x,(elapsed+750)*1000))break;progress=true;}
                // Tunnel output usually renders independently. If an OEM returns buffers, relinquish them.
                if(pollOutput)try{for(int i=0;i<4;i++){int id=c.dequeueOutputBuffer(out,0);if(id>=0){c.releaseOutputBuffer(id,out.size>0);unexpectedOutputs++;if(unexpectedOutputs==1)log("VIDEO_OUTPUT_BUFFER returned in tunnel mode");}else if(id==MediaCodec.INFO_OUTPUT_FORMAT_CHANGED){log("VIDEO_OUTPUT_FORMAT "+c.getOutputFormat());}else break;}}catch(IllegalStateException e){pollOutput=false;log("VIDEO_OUTPUT_POLL disabled: "+e);}
                for(int i=0;i<128&&unit!=null&&!cancel;i++){
                    if(unit.ptsNs/1000000>elapsed+500)break;
                    if(!ab.hasRemaining()){units++;unit=feed.next();if(unit==null)break;ab.clear();ab.put(unit.data).flip();if(unit.ptsNs/1000000>elapsed+500)break;}
                    int n=a.write(ab,ab.remaining(),AudioTrack.WRITE_NON_BLOCKING,unit.ptsNs);if(n<0)throw new IOException("AudioTrack.write="+n+" units="+units+" bytes="+bytes);
                    if(n==0)break;bytes+=n;lastProgress=now;progress=true;
                }
                if(now-lastReport>=1000){lastReport=now;log("TUNNEL_PROGRESS session="+session+" elapsedMs="+elapsed+" bytes="+bytes+" audioUnits="+units+" audioPtsNs="+(unit==null?-1:unit.ptsNs)+" videoQueued="+videoFeed.frames+" videoPtsUs="+videoFeed.lastPtsUs+" firstReady="+firstReady.get()+" renderedCallbacks="+rendered.get()+" lastRenderedUs="+lastRendered.get()+" "+audioStatus(a));}
                if(unit!=null&&now-lastProgress>STALL_MS)throw new IOException("No audio write progress for 8s; session="+session+" bytes="+bytes+" videoQueued="+videoFeed.frames+" rendered="+rendered.get()+" "+audioStatus(a));
                if(unit==null&&videoFeed.done)break;if(!progress)SystemClock.sleep(2);
            }
            log("TUNNEL_END session="+session+" stopped="+cancel+" bytes="+bytes+" audioUnits="+units+" videoQueued="+videoFeed.frames+" firstReady="+firstReady.get()+" renderedCallbacks="+rendered.get()+" lastRenderedUs="+lastRendered.get()+" "+audioStatus(a));
        }finally{
            if(a!=null){try{a.pause();}catch(Exception ignored){}try{a.flush();}catch(Exception ignored){}}
            if(c!=null){if(started)try{c.stop();}catch(Exception e){log("VIDEO_STOP failed "+e);}try{c.release();}catch(Exception ignored){}}
            if(a!=null)try{a.release();}catch(Exception ignored){}x.release();log("=== TUNNEL TEST END "+m.name()+" session="+session+" ===");
        }
    }
}
