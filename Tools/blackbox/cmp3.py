import sys,os,subprocess; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np;from an3 import *
import os; H=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','build-dsp','TranceraRenderEvents')
c3=np.load('cap3.npz');c3c=np.load('cap3c.npz');c4=np.load('cap4.npz');c5=np.load('cap5.npz');LAT=0.0957
BASE=['0 node digital','0 set balance -64','0 set amp.a 0','0 set amp.s 127','0 set amp.r 0','0 set fenv.s 127','0 set velocityamp 0','0 set level 100','0 set filter.steep 0','0 set fenv.a 0','0 set envdepth 0']
TRI={'osc1.wave':6,'osc1.ctrl1':0};FC={'osc1.wave':2,'osc1.ctrl1':127,'osc1.ctrl2':0,'resonance':112}
def ours(sets,notes,dur):
    ev=BASE+[f'0 set {k} {v}' for k,v in sets.items()]
    tl=[]
    for t,n,l in notes:tl+=[(t,f'{t} note {n} 100'),(t+l,f'{t+l} off {n}')]
    ev+=[e for _,e in sorted(tl,key=lambda q:q[0])]
    open('ev3.txt','w').write('\n'.join(ev)+'\n');subprocess.run([H,'ev3.txt','o3.f32',str(SR),str(dur)],check=True)
    x=np.fromfile('o3.f32','<f4').astype(float);return np.r_[np.zeros(int(LAT*SR)),x]  # align to the emulator's latency
def cyc(x,note,t0):
    P=int(round(SR/(440*2**((note-69)/12))));o=int(t0*SR);s=abs(x[o:]);n=len(s)//P;pk=s[:n*P].reshape(n,P).max(1);return (np.arange(n)*P+P/2)/SR,pk
def att(x):
    t,e=cyc(x,60,0.05+LAT);e=e/e.max();return [t[np.argmax(e>=q)] for q in (0.5,0.9)]
def rel(x,note=60,toff=0.35):
    t,e=cyc(x,note,toff+LAT-0.003);ref=abs(x[int((toff+LAT-0.05)*SR):int((toff+LAT-0.003)*SR)]).max();db=20*np.log10(e/ref+1e-9)
    m=(db<-1)&(db>-30)&(np.arange(len(db))<len(db)-3);a=np.polyfit(t[m],db[m],1)[0];return -60/a
def pitch_track(x,f0):
    t,f=zc_pitch(x);return t,12*np.log2(f/f0)
rows=[];f72=440*2**(3/12)
def row(name,em,us,unit,fmt='{:.3g}'):
    rows.append((name,em,us,unit));print(f'{name:28s} emulator {fmt.format(em):>9s}  ours {fmt.format(us):>9s} {unit}   gap {100*(us/em-1) if em else 0:+.1f} %',flush=True)
for v in (16,40,64,88,112):
    a=att(c4[f'aa_{v:03d}']);b=att(ours({**TRI,'amp.a':v},[(0.05,60,2.4*np.interp(v,[0,64,112],[0.05,0.6,4.5]))],2.6*np.interp(v,[0,64,112],[0.05,0.6,4.5])+0.3))
    row(f'amp attack {v} t50',a[0],b[0],'s');row(f'amp attack {v} t90',a[1],b[1],'s')
for v in (12,32,64,96,116):
    src=c5 if v>112 else c4;L=min(12,np.interp(v,[0,64,112,116],[0.1,0.8,6.1,7.5]))
    row(f'amp release {v} T60',rel(src[f'ar_{v:03d}']),rel(ours({**TRI,'amp.r':v},[(0.05,60,0.3)],0.35+L)),'s')
for v in (48,96):
    def dec(x):
        t,e=cyc(x,60,0.5+LAT);i=np.argmax(e);db=20*np.log10(e/e[i]+1e-9);m=(np.arange(len(e))>i)&(db<-1)&(db>-30);return -60/np.polyfit(t[m],db[m],1)[0]
    x=c3c[f'ad_{v:03d}'];row(f'amp decay {v} T60 (S=0)',dec(x),dec(ours({**TRI,'amp.s':0,'amp.d':v},[(0.05,60,0.3),(0.5,60,3)],4)),'s')
f72=440*2**(3/12)
for v in (56,64,100):
    def rate(x):
        t,s=pitch_track(x,f72);m=t>0.3+LAT;t,s=t[m],s[m];tu=np.arange(t[0],t[-1],0.0005);su=np.interp(tu,t,s)-np.median(s)
        z=np.where((su[:-1]<0)&(su[1:]>=0))[0];return (len(z)-1)/(tu[z[-1]]-tu[z[0]])
    L={56:1.4,64:1.4,100:1.4}[v];row(f'LFO1 rate {v}',rate(c4[f'lr_{v:03d}']),rate(ours({**TRI,'pitchlfo':24,'lfo1.rate':v},[(0.05,72,L+0.2)],L+0.4)),'Hz')
def ppk(x):
    t,s=pitch_track(x,f72);t=t-0.05-LAT;return abs(np.interp(0.01,t,s)),abs(np.interp(0.2,t,s))
for dd in (20,-24,44):
    a=ppk(c4[f'pe_d{dd:+03d}']);b=ppk(ours({**TRI,'pitchenv':dd,'pitchenv.d':64},[(0.05,72,1.6)],1.8));row(f'pitch env {dd} at 10 ms',a[0],b[0],'st');row(f'pitch env {dd} at 200 ms',a[1],b[1],'st')
for v in (32,96):
    x=c3[f'pd_{v:03d}'];L=float(np.clip(1.5*0.004*3000**(v/127),1,9))
    def hl(x):
        t,s=pitch_track(x,f72);t=t-0.05-LAT;m=t>0.002;t,s=t[m],s[m];i=np.argmax(s>0.5*14.5*0+0) ;pk=np.median(s[:5]);return t[np.argmax(s<pk/2)]
    row(f'pitch env decay {v} half',hl(x),hl(ours({**TRI,'pitchenv':32,'pitchenv.d':v},[(0.05,72,L)],L+0.2)),'s')
for dd in (20,44):
    base=24;a=knob(fc_static(c4[f'fe_d{dd:+03d}']))-base;b=knob(fc_static(ours({**FC,'cutoff':base,'envdepth':dd},[(0.05,60,0.9)],1.1)))-base;row(f'filter env {dd} shift',a,b,'steps')
for kf in (-48,40):
    for n in (36,84):
        a=knob(fc_static(c4[f'kf_k{kf:+03d}_n{n}']));b=knob(fc_static(ours({**FC,'cutoff':64,'keyfollow':kf},[(0.05,n,0.9)],1.1)));row(f'key follow {kf} note {n}',a,b,'steps')
def lfa(x):
    t,f=fc_track(x,2048,256);k=knob(np.maximum(f,20))[(t>0.4+LAT)&(t<2.7+LAT)];lo,hi=np.percentile(k,[1,99]);return (hi-lo)/2
row('filter LFO 24 swing',lfa(c4['lf_d+24']),lfa(ours({**FC,'cutoff':64,'filterlfo':24,'lfo1.rate':48},[(0.05,60,2.8)],3.0)),'steps')
f48=440*2**(-21/12)
for v in (32,48,64):
    def g50(x):
        t,s=pitch_track(x,f48);t=t-0.6-LAT;m=t>-0.03;t,s=t[m],s[m];tu=np.arange(-0.03,t[-1]-0.1,0.0005);su=np.interp(tu,t,s);return tu[np.argmax(su>=12)]
    L=1.2;row(f'portamento {v} 24 st half',g50(c4[f'po24_{v:03d}']),g50(ours({'osc1.wave':6,'mono':1,'portamento':1,'portamento.time':v},[(0.05,48,0.7),(0.6,72,L)],L+0.8)),'s')
g=np.array([abs(us/em-1) for _,em,us,_ in rows if em]);print('median gap',round(100*np.median(g),1),'% worst',round(100*g.max(),1),'%')
np.save('cmp3.npy',np.array(rows,dtype=object),allow_pickle=True)
