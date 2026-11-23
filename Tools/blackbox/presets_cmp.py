import sys,os,json,subprocess; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np,presets_map as m
SR=88200;LAT=0.0957;P=sys.argv[1];OUTD=sys.argv[2]
H=os.environ.get('RENDER') or os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','build-dsp','TranceraRenderEvents')
LABEL=os.environ.get('LABEL','Trancera')
V=os.environ.get('PLAIN','');d=json.load(open(f'{P}/presets{V}.json'))
def ours(params,notes,dur=6.5):
    ev=['0 node digital']+m.events(params,matrix=not V and os.environ.get('MATRIX','1')=='1');tl=[]
    for t,n,v,l in notes:tl+=[(t,f'{t} note {n} {v}'),(t+l,f'{t+l} off {n}')]
    ev+=[e for _,e in sorted(tl,key=lambda q:q[0])]
    open(f'{P}/ev.txt','w').write('\n'.join(ev)+'\n');subprocess.run([H,f'{P}/ev.txt',f'{P}/o.f32',str(SR),str(dur),'stereo'],check=True)
    x=np.fromfile(f'{P}/o.f32','<f4').astype(float).reshape(-1,2).T;return np.concatenate([np.zeros((2,int(LAT*SR))),x],1)
B=[63*2**(k/3) for k in range(0,26)]
def bands(x):
    y=x.mean(0);n=8192;w=np.hanning(n);S=np.mean([np.abs(np.fft.rfft(y[i:i+n]*w))**2 for i in range(0,len(y)-n,n//2)],0);f=np.fft.rfftfreq(n,1/SR)
    return np.array([10*np.log10(S[(f>=a)&(f<a*2**(1/3))].sum()+1e-20) for a in B])
rows=[];out=[];key=[];t=0.0;gap=np.zeros((2,int(0.6*SR)))
for k,v in d.items():
    a=np.load(f'{P}/je{V}_{int(k):03d}.npy').astype(float);b=ours(v['params'],[tuple(q) for q in v['notes']])
    n=min(a.shape[1],b.shape[1]);a=a[:,:n];b=b[:,:n]
    raw=20*np.log10(b.std()/a.std());g=a.std()/b.std()
    ba=bands(a);bb=bands(b*g);dif=bb-ba;w=ba>ba.max()-50
    tilt_lo=np.mean(dif[(np.array(B)<300)&w]) if ((np.array(B)<300)&w).any() else np.nan
    tilt_mid=np.mean(dif[(np.array(B)>=300)&(np.array(B)<3000)&w]);tilt_hi=np.mean(dif[(np.array(B)>=3000)&w])
    # envelope match: correlation of 10 ms RMS envelopes
    ea=np.sqrt(np.convolve(a.mean(0)**2,np.ones(882)/882,'same')[::441]);eb=np.sqrt(np.convolve((g*b).mean(0)**2,np.ones(882)/882,'same')[::441])
    env=20*np.log10((np.abs(ea-eb).mean()+1e-9)/(ea.mean()+1e-9))
    nm=m.name(v['params']);rows.append((nm,raw,tilt_lo,tilt_mid,tilt_hi,env))
    print(f'{nm:16s} level ours-emu {raw:+5.1f} dB | tone (level-matched) <300 Hz {tilt_lo:+5.1f}  0.3-3k {tilt_mid:+5.1f}  >3k {tilt_hi:+5.1f} dB | envelope error {env:+5.1f} dB',flush=True)
    for lab,x in (('A emulator',a),('B '+LABEL,b*g)):
        key.append(f'{t:6.1f} s  {lab:14s} {k:>3s} {nm}'+(f'   (raw level {raw:+.1f} dB)' if lab[0]=='B' else ''));out+=[x,gap];t+=(x.shape[1]+gap.shape[1])/SR
x=np.concatenate(out,1);x*=0.5/abs(x).max();os.makedirs(OUTD,exist_ok=True)
subprocess.run(['ffmpeg','-v','error','-y','-f','f32le','-ar',str(SR),'-ac','2','-i','-','-ar','48000','-c:a','pcm_s24le',f'{OUTD}/ab-factory-patches.wav'],input=x.T.astype('<f4').tobytes(),check=True)
txt='A/B: JE-8086 factory patches (part A, played SINGLE) against '+LABEL+' with the same settings, digital output, stereo.\nPhrase: Am F C G chords, then an eight-note line. Emulator first; Trancera level-matched per patch (raw level difference noted).\nVelocity is 100 on both; Trancera plays the velocity matrix unless PLAIN (both off) or MATRIX=0.\n\n'+'\n'.join(key)+'\n\nPer patch: raw level, tone difference by region after level matching, envelope error (mean abs difference of 10 ms envelopes over mean level):\n'+'\n'.join(f'{r[0]:16s} level {r[1]:+5.1f} dB | <300 Hz {r[2]:+5.1f} | 0.3-3 kHz {r[3]:+5.1f} | >3 kHz {r[4]:+5.1f} | envelope {r[5]:+5.1f} dB' for r in rows)+'\n'
open(f'{OUTD}/KEY.txt','w').write(txt);print(len(x[0])/SR,'s')
