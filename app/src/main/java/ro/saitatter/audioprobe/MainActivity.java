package ro.saitatter.audioprobe;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Bundle;
import android.os.Build;
import android.os.SystemClock;
import android.media.*;
import android.util.Log;
import android.view.WindowManager;
import android.view.View;
import android.widget.*;
import java.io.*;
import java.nio.ByteBuffer;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final String TAG="AudioProbe";
    private static final long RUN_MS=20000, NO_PROGRESS_MS=3000;
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private volatile boolean cancel;
    private boolean busy;
    private File dir, logFile;
    private Spinner modes, outcome;
    private TextView output, state;
    private ScrollView scroll;
    private Button inspect, open, play, stop, mark, tunnel, matTunnel, codecInspect;
    private AudioManager manager;
    private final Object logLock=new Object();

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        manager=(AudioManager)getSystemService(AUDIO_SERVICE);
        setVolumeControlStream(AudioManager.STREAM_MUSIC);
        dir=getExternalFilesDir(null);
        if(dir==null) dir=getFilesDir();
        logFile=new File(dir,"probe.log");
        LinearLayout root=new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL); root.setPadding(24,16,24,16);
        TextView title=new TextView(this); title.setText("Audio Passthrough Tester  •  v0.2.4"); title.setTextSize(25); root.addView(title);
        TextView warn=new TextView(this); warn.setText("Turn soundbar volume LOW. Select eARC. STOP immediately if noise occurs. No automatic playback."); warn.setTextSize(15); root.addView(warn);
        matTunnel=new Button(this);
        matTunnel.setText("MAT 192 kHz — 3 current tests"); matTunnel.setAllCaps(false);
        matTunnel.setOnClickListener(v->startActivity(new android.content.Intent(this,TunnelActivity.class).putExtra(TunnelActivity.EXTRA_MAT_ONLY,true)));
        codecInspect=new Button(this);
        codecInspect.setText("Inspect audio codecs — no playback"); codecInspect.setAllCaps(false);
        codecInspect.setOnClickListener(v->runJob(this::inspectAudioCodecs));
        Button syncCal=new Button(this);
        syncCal.setText("A/V Sync Calibrator (eARC TrueHD Atmos rhythm test)"); syncCal.setAllCaps(false);
        syncCal.setOnClickListener(v->startActivity(new android.content.Intent(this,SyncCalibratorActivity.class)));
        root.addView(syncCal,new LinearLayout.LayoutParams(-1,-2));
        root.addView(codecInspect,new LinearLayout.LayoutParams(-1,-2));
        root.addView(matTunnel,new LinearLayout.LayoutParams(-1,-2));
        TextView previous=new TextView(this); previous.setText("Previous diagnostics (optional)"); root.addView(previous);
        modes=new Spinner(this); ArrayAdapter<ProbeMode> choices=new ArrayAdapter<>(this,android.R.layout.simple_spinner_item,ProbeMode.values()); choices.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item); modes.setAdapter(choices); root.addView(modes);
        LinearLayout buttons=new LinearLayout(this); root.addView(buttons);
        inspect=button(buttons,"Capabilities",v->runJob(this::capabilities));
        open=button(buttons,"Open only",v->{ ProbeMode m=selected(); runJob(()->test(m,false)); });
        play=button(buttons,"Play 20 sec",v->confirm());
        stop=button(buttons,"STOP",v->{ cancel=true; log("USER STOP requested"); }); stop.setEnabled(false);
        tunnel=button(buttons,"All A/V tests",v->startActivity(new android.content.Intent(this,TunnelActivity.class)));
        LinearLayout results=new LinearLayout(this); root.addView(results);
        outcome=new Spinner(this); ArrayAdapter<String> options=new ArrayAdapter<>(this,android.R.layout.simple_spinner_item,new String[]{"Select observed result","Dolby Atmos + normal sound","Dolby Surround + normal sound","PCM + normal sound","No sound","Beep / noise","Other / failed"}); options.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item); outcome.setAdapter(options); results.addView(outcome,new LinearLayout.LayoutParams(0,-2,1));
        mark=button(results,"Record result",v->{ if(outcome.getSelectedItemPosition()>0) log("OBSERVED by user ["+selected().name()+"]: "+outcome.getSelectedItem()); });
        state=new TextView(this); root.addView(state); state.setText("Ready | samples and report: "+dir);
        scroll=new ScrollView(this); output=new TextView(this); output.setTextSize(13); output.setTypeface(android.graphics.Typeface.MONOSPACE); output.setTextIsSelectable(false); scroll.addView(output); root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        setContentView(root); codecInspect.requestFocus();
        log("START app="+getPackageName()+" pid="+android.os.Process.myPid()+" model="+Build.MODEL+" fingerprint="+Build.FINGERPRINT+" ABIs="+Arrays.toString(Build.SUPPORTED_ABIS));
        log("Samples: "+dir+" (prepare on PC, then adb push)");
    }
    private Button button(LinearLayout row,String title,View.OnClickListener listener) {
        Button b=new Button(this); b.setText(title); b.setAllCaps(false); b.setOnClickListener(listener); row.addView(b,new LinearLayout.LayoutParams(0,-2,1)); return b;
    }
    private ProbeMode selected(){ return (ProbeMode)modes.getSelectedItem(); }
    private void confirm(){ ProbeMode m=selected(); new AlertDialog.Builder(this).setTitle("Play "+m.name()+"?").setMessage("Lower the physical soundbar volume first. This sends the exact selected encoding; a broken HAL could output noise. Use STOP immediately if necessary.").setNegativeButton("Cancel",null).setPositiveButton("Start",(d,w)->runJob(()->test(m,true))).show(); }
    private void log(String msg){
        String line=new SimpleDateFormat("HH:mm:ss.SSS",Locale.US).format(new Date())+" "+msg;
        Log.i(TAG,msg);
        synchronized(logLock){
            try(FileWriter fw=new FileWriter(logFile,true)){fw.write(line+"\n");}catch(IOException e){Log.e(TAG,"Cannot save report",e);}
        }
        runOnUiThread(()->{if(output==null)return; if(output.length()>32000)output.setText(output.getText().subSequence(output.length()-20000,output.length())); output.append(line+"\n"); scroll.post(()->scroll.fullScroll(View.FOCUS_DOWN));});
    }
    private interface Job { void run() throws Exception; }
    private void runJob(Job job){
        if(busy) return; busy=true; cancel=false; controls(false);
        worker.execute(()->{try{job.run();}catch(Exception e){log("FAIL: "+e.getClass().getSimpleName()+": "+e.getMessage()); Log.e(TAG,"Job failed",e);}finally{runOnUiThread(()->{busy=false; controls(true);});}});
    }
    private void controls(boolean enabled){ tunnel.setEnabled(enabled); matTunnel.setEnabled(enabled); codecInspect.setEnabled(enabled); inspect.setEnabled(enabled); open.setEnabled(enabled); play.setEnabled(enabled); modes.setEnabled(enabled); mark.setEnabled(enabled); stop.setEnabled(!enabled); state.setText(enabled?"Ready | result: "+logFile:"Running — press STOP if noise occurs"); if(!enabled)stop.requestFocus(); else play.requestFocus(); }
    private String device(AudioDeviceInfo d) {return d==null?"none":("id="+d.getId()+" type="+d.getType()+" name="+d.getProductName());}
    private void inspectAudioCodecs() throws IOException {
        File report=new File(dir,"codec_routes.log");
        try(PrintWriter w=new PrintWriter(new FileWriter(report,false))) {
            w.println("Audio codec inventory v0.2.4 fingerprint="+Build.FINGERPRINT);
            w.println("No codec is instantiated or playback started.");
            for(int kind:new int[]{MediaCodecList.REGULAR_CODECS,MediaCodecList.ALL_CODECS}){
                w.println("=== "+(kind==MediaCodecList.ALL_CODECS?"ALL_CODECS":"REGULAR_CODECS")+" ===");
                MediaCodecList list=new MediaCodecList(kind);
                for(MediaCodecInfo info:list.getCodecInfos()){
                    if(cancel)break;
                    if(info.isEncoder())continue;
                    for(String type:info.getSupportedTypes()){
                        if(!type.toLowerCase(Locale.US).startsWith("audio/"))continue;
                        w.println("DECODER "+info.getName()+" canonical="+info.getCanonicalName()+" vendor="+info.isVendor()+" alias="+info.isAlias()+" type="+type);
                        try{
                            MediaCodecInfo.AudioCapabilities a=info.getCapabilitiesForType(type).getAudioCapabilities();
                            if(a!=null)w.println("  channelsMax="+a.getMaxInputChannelCount()+" sampleRates="+Arrays.toString(a.getSupportedSampleRateRanges())+" bitrate="+a.getBitrateRange());
                        }catch(Exception e){w.println("  capabilities error="+e);}
                    }
                }
                for(String mime:new String[]{"audio/true-hd","audio/vnd.dolby.mlp","audio/truehd","audio/vnd.dolby.mat","audio/ac3","audio/eac3","audio/eac3-joc"}){
                    if(cancel)break;
                    try{
                        MediaFormat format=new MediaFormat(); format.setString(MediaFormat.KEY_MIME,mime);
                        w.println("FIND mime="+mime+" decoder="+list.findDecoderForFormat(format));
                    }catch(Exception e){w.println("FIND mime="+mime+" error="+e);}
                    int channels=mime.equals("audio/ac3")||mime.startsWith("audio/eac3")?6:8;
                    try{
                        MediaFormat format=MediaFormat.createAudioFormat(mime,48000,channels);
                        w.println("FIND mime="+mime+" rate=48000 channels="+channels+" decoder="+list.findDecoderForFormat(format));
                    }catch(Exception e){w.println("FIND mime="+mime+" 48000 error="+e);}
                    if(mime.equals("audio/vnd.dolby.mlp")||mime.equals("audio/vnd.dolby.mat")){
                        try{
                            w.println("FIND mime="+mime+" rate=192000 channels=8 decoder="+list.findDecoderForFormat(MediaFormat.createAudioFormat(mime,192000,8)));
                        }catch(Exception e){w.println("FIND mime="+mime+" 192000 error="+e);}
                    }
                }
            }
            w.println("END cancelled="+cancel);
            if(w.checkError())throw new IOException("Could not write codec inventory");
        }
        log("Audio codec inventory saved: "+report);
    }
    private void capabilities(){
        log("=== CAPABILITIES ===");
        for(AudioDeviceInfo d:manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)){
            log(device(d)+" encodings="+Arrays.toString(d.getEncodings())+" rates="+Arrays.toString(d.getSampleRates())+" masks="+Arrays.toString(d.getChannelMasks())+" channels="+Arrays.toString(d.getChannelCounts()));
            for(AudioProfile p:d.getAudioProfiles()) log("  device profile encoding="+p.getFormat()+" rates="+Arrays.toString(p.getSampleRates())+" masks="+Arrays.toString(p.getChannelMasks()));
        }
        if(Build.VERSION.SDK_INT>=33){for(ProbeMode m:new ProbeMode[]{ProbeMode.TRUEHD_RAW,ProbeMode.TRUEHD_SYNC}){
            try{ for(AudioProfile p:manager.getDirectProfilesForAttributes(m.attrs())) log("directProfiles sync="+m.sync+" encoding="+p.getFormat()+" rates="+Arrays.toString(p.getSampleRates())+" masks="+Arrays.toString(p.getChannelMasks())); }catch(Exception e){log("directProfiles failed: "+e);}
        }}
        for(ProbeMode m:ProbeMode.values()){ if(cancel)break; query(m); }
        log("=== CAPABILITIES END ===");
    }
    private void query(ProbeMode m){
        try{AudioFormat f=m.format(); AudioAttributes a=m.attrs();
            boolean supported=AudioTrack.isDirectPlaybackSupported(f,a);
            String bits=Build.VERSION.SDK_INT>=33?Integer.toHexString(AudioManager.getDirectPlaybackSupport(f,a)):"n/a";
            log("QUERY "+m.name()+" enc="+m.encoding+" rate="+m.rate+" mask=0x"+Integer.toHexString(m.mask)+" direct="+supported+" directSupport=0x"+bits+" minBuffer="+AudioTrack.getMinBufferSize(m.rate,m.mask,m.encoding));
        }catch(Exception e){log("QUERY "+m.name()+" rejected: "+e);}
    }
    private AudioTrack create(ProbeMode m) {
        AudioFormat f=m.format(); AudioAttributes a=m.attrs();
        int min;
        try { min=AudioTrack.getMinBufferSize(m.rate,m.mask,m.encoding); } catch(Exception e) { min=-1; log("minBuffer query failed: "+e); }
        int size=Math.max(min, m.kind==FrameReader.Kind.PCM?32768:245760);
        size=(size+15)&~15;
        // Deliberately try regardless of advertised capability or getMinBufferSize error.
        // Builder may itself reject the configuration. No hidden API or PCM masquerading.
        log("BUILD "+m.name()+" requestedBuffer="+size+" requestedOffload="+m.offload+" HW_AV_SYNC="+m.sync+" min="+min);
        return new AudioTrack.Builder().setAudioFormat(f).setAudioAttributes(a).setTransferMode(AudioTrack.MODE_STREAM).setBufferSizeInBytes(size).setOffloadedPlayback(m.offload).build();
    }
    private void test(ProbeMode m,boolean actuallyPlay) throws Exception {
        log("=== TEST "+m.name()+" play="+actuallyPlay+" ==="); query(m);
        AudioTrack at=null;
        try{
            at=create(m);
            log("BUILT session="+at.getAudioSessionId()+" state="+at.getState()+" encoding="+at.getAudioFormat()+" rate="+at.getSampleRate()+" channels="+at.getChannelCount()+" bufferFrames="+at.getBufferSizeInFrames()+" isOffloadedPlayback="+at.isOffloadedPlayback());
            if(at.getState()!=AudioTrack.STATE_INITIALIZED)throw new IOException("Track not initialized");
            if(!actuallyPlay){log("OPEN_ONLY accepted; no playback or route validation"); return;}
            if(m.label.contains("OPEN ONLY")) throw new IOException("This mode is a capability probe only; use Open only. Carrier is not validated for this configuration.");
            File f=new File(dir,m.file); if(!f.isFile())throw new FileNotFoundException(f.toString());
            long total=0, units=0, lastReport=0;
            try(FrameReader reader=new FrameReader(f,m.kind)){
                // Validate first AU/packet before opening the playback path.
                FrameReader.Unit u=reader.next(); if(u==null)throw new IOException("Empty test file");
                at.play(); long start=SystemClock.elapsedRealtime(), lastProgress=start;
                log("PLAY started; route="+device(at.getRoutedDevice())+" file="+f.getName()+" bytes="+f.length());
                ByteBuffer buf=ByteBuffer.allocateDirect(65536);
                while(u!=null && !cancel && SystemClock.elapsedRealtime()-start<RUN_MS){
                    if(u.ptsNs>RUN_MS*1000000L)break; // avoid queuing the whole clip too fast
                    buf.clear(); buf.put(u.data); buf.flip();
                    while(buf.hasRemaining()&&!cancel&&SystemClock.elapsedRealtime()-start<RUN_MS){
                        int n=m.sync?at.write(buf,buf.remaining(),AudioTrack.WRITE_NON_BLOCKING,u.ptsNs):at.write(buf,buf.remaining(),AudioTrack.WRITE_NON_BLOCKING);
                        long now=SystemClock.elapsedRealtime();
                        if(n<0)throw new IOException("AudioTrack.write error="+n+" unit="+units+" total="+total);
                        if(n>0){total+=n;lastProgress=now;}
                        else {if(now-lastProgress>NO_PROGRESS_MS)throw new IOException("No write progress for 3s"); SystemClock.sleep(5);}
                        if(now-lastReport>=1000){lastReport=now; log("PROGRESS bytes="+total+" completeUnits="+units+" "+trackStatus(at));}
                    }
                    if(!buf.hasRemaining())units++;
                    // Keep writes no more than ~250 ms ahead of the media clock, rather than flooding offload.
                    while(!cancel&&SystemClock.elapsedRealtime()-start<RUN_MS&&u.ptsNs/1000000L>SystemClock.elapsedRealtime()-start+250)SystemClock.sleep(5);
                    u=reader.next();
                }
                if(!cancel) {long until=Math.min(start+RUN_MS,SystemClock.elapsedRealtime()+600);while(!cancel&&SystemClock.elapsedRealtime()<until)SystemClock.sleep(20);}
                log("END bytes="+total+" units="+units+" cancelled="+cancel+" "+trackStatus(at));
            }
        } finally {if(at!=null){try{at.pause();}catch(Exception ignored){}try{at.flush();}catch(Exception ignored){}try{at.release();}catch(Exception ignored){}} log("=== TEST END "+m.name()+" ===");}
    }
    private String trackStatus(AudioTrack at){
        try { AudioTimestamp ts=new AudioTimestamp(); boolean valid=at.getTimestamp(ts); return "head="+Integer.toUnsignedLong(at.getPlaybackHeadPosition())+" timestamp="+(valid?ts.framePosition+"/"+ts.nanoTime:"unavailable")+" route="+device(at.getRoutedDevice()); }
        catch(Exception e){return "telemetry unavailable: "+e;}
    }
    @Override protected void onStop(){cancel=true;super.onStop();}
    @Override protected void onDestroy(){cancel=true;worker.shutdownNow();super.onDestroy();}
}
