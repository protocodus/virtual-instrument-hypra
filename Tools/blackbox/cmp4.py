import sys,os,subprocess; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np;from an3 import *
from an import amp as tone
import os; H=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','build-dsp','TranceraRenderEvents')
d=np.load('cap6.npz');LAT=0.0957
BASE=['0 node digital','0 set balance -64','0 set amp.a 0','0 set amp.s 127','0 set amp.r 0','0 set fenv.s 127','0 set velocityamp 0','0 set level 100','0 set filter.steep 0','0 set fenv.a 0','0 set envdepth 0']
TRI={'osc1.wave':6,'osc1.ctrl1':0}
def ours(sets,notes,dur,extra=(),stereo=False):
    ev=BASE+[f'0 set {k} {v}' for k,v in sets.items()]+list(extra);tl=[]
    for t,n,l,*vel in notes:tl+=[(t,f'{t} note {n} {vel[0] if vel else 100}'),(t+l,f'{t+l} off {n}')]
    ev+=[e for _,e in sorted(tl,key=lambda q:q[0])]
    open('ev4.txt','w').write('\n'.join(ev)+'\n');subprocess.run([H,'ev4.txt','o4.f32',str(SR),str(dur)]+(['stereo'] if stereo else []),check=True)
    x=np.fromfile('o4.f32','<f4').astype(float);pad=int(LAT*SR)
    if stereo:x=x.reshape(-1,2).T;return np.concatenate([np.zeros((2,pad)),x],1)
    return np.r_[np.zeros(pad),x]
rows=[]
def row(name,em,us,unit):
    rows.append((name,em,us));g=100*(us/em-1) if em else 0;print(f'{name:34s} emulator {em:9.4g}  ours {us:9.4g} {unit:6s} gap {g:+.1f} %',flush=True)
P84=int(round(SR/(440*2**(15/12))))
def cyc(x,P,t0):
    o=int(t0*SR);s=abs(x[o:]);n=len(s)//P;return (np.arange(n)*P+P/2)/SR,s[:n*P].reshape(n,P).max(1)
for v in (16,24,32,48,64):
    def tt(x):
        t,e=cyc(x,P84,0.05+LAT-0.01);t=t-0.01;e=e/np.percentile(e,97);return t[np.argmax(e>=0.5)],t[np.argmax(e>=0.9)]
    a=tt(d[f'sa_{v:03d}']);L=1.2 if v>=48 else 0.5;b=tt(ours({**TRI,'amp.a':v},[(0.05,84,L)],L+0.2))
    row(f'attack {v} t50 (C6)',a[0],b[0],'s');row(f'attack {v} t90 (C6)',a[1],b[1],'s')
for v in (16,32,64):
    def rl(x):
        t,e=cyc(x,P84,0.35+LAT-0.02);t=t-0.02;db=20*np.log10(e/np.median(e[:4])+1e-9);m=(db<-3)&(db>-40)&(np.arange(len(db))<len(db)-3);b=np.polyfit(t[m],db[m],1);return -b[1]/b[0]
    row(f'release {v} start delay',rl(d[f'sr_{v:03d}']),rl(ours({**TRI,'amp.r':v},[(0.05,84,0.3)],1.0)),'s')
f72=440*2**(3/12)
def fadefit(x):
    t,f=zc_pitch(x);s=12*np.log2(f/f72);t=t-0.05-LAT;P=1/9.091/2;edges=np.arange(0.11,t[-1]-0.05,P)
    a=np.array([np.abs(s[(t>=q)&(t<q+P)]).max() for q in edges])/3.92;tt=edges+P/2;m=(a>0.015)&(a<0.85);k,b=np.polyfit(tt[m],20*np.log10(a[m]),1);return -b/k
for v in (80,96,112):
    row(f'fade {v} reaches full',fadefit(d[f'fa_{v:03d}']),fadefit(ours({**TRI,'pitchlfo':24,'lfo1.rate':96,'lfo1.fade':v},[(0.05,72,6.0)],6.2)),'s')
def steps(x):
    t,f=zc_pitch(x);s=12*np.log2(f/f72);t=t-0.05-LAT;tu=np.arange(0.2,2.4,0.002);su=np.interp(tu,t,s);j=np.abs(np.diff(su))>0.05;st=np.sum(j[1:]&~j[:-1]);return st/(tu[-1]-tu[0])
row('random LFO steps per s (rate 64)',steps(d['lw_random']),steps(ours({**TRI,'pitchlfo':24,'lfo1.rate':64,'lfo1.wave':3},[(0.05,72,2.5)],2.7)),'1/s')
def rate(x):
    t,f=zc_pitch(x);s=12*np.log2(f/f72);t=t-0.05-LAT;m=(t>0.15)&(t<t[-1]-0.05);t,s=t[m],s[m];tu=np.arange(t[0],t[-1],0.0005);su=np.interp(tu,t,s);su-=su.mean()
    N=len(su)*32;F=np.abs(np.fft.rfft(su*np.hanning(len(su)),N));fr=np.fft.rfftfreq(N,0.0005);return fr[np.argmax(F[1:])+1]
def swing(x):
    t,f=zc_pitch(x);s=12*np.log2(f/f72);t=t-0.05-LAT;m=(t>0.3)&(t<t[-1]-0.05);tu=np.arange(t[m][0],t[m][-1],0.0005);su=np.interp(tu,t[m],s[m]);return (np.percentile(su,99.7)-np.percentile(su,0.3))/2
for v in (0,64,112):
    L=float(np.clip(2.5/(0.05*500**(v/127)),1.5,12));row(f'LFO 2 rate {v}',rate(d[f'l2r_{v:03d}']),rate(ours({**TRI,'lfo2.pitch':24,'lfo2.rate':v},[(0.05,72,L)],L+0.2,['0 mod 127'])),'Hz')
row('LFO 2 pitch 63 at mod 64',swing(d['l2m_064']),swing(ours({**TRI,'lfo2.pitch':63,'lfo2.rate':80},[(0.05,72,2.0)],2.2,['0 mod 64'])),'st')
P72=int(round(SR/f72))
def gains(x,ref):
    t,e=cyc(x,P72,0.05+LAT+0.2);e=e[:int(1.2*SR/P72)]/ref;return e.min(),e.max()
for dd in (32,63):
    a=gains(d[f'st_al_{dd:+03d}'][0],0.1098);rx=ours(TRI,[(0.05,72,1.0)],1.2);ref=cyc(rx,P72,0.05+LAT+0.2)[1][:50].max()
    b=gains(ours({**TRI,'amplfo':dd,'lfo1.rate':64},[(0.05,72,1.5)],1.7),ref);row(f'amp LFO {dd} lowest gain',a[0],b[0],'x');row(f'amp LFO {dd} highest gain',a[1],b[1],'x')
cen=ours({**TRI,'pan':2,'amplfo':0},[(0.05,72,0.8)],1.0,stereo=True);cref=cyc(cen[0],P72,0.05+LAT+0.2)[1].max()
for dd in (-32,32,63):
    x=d[f'st_pan_man_{dd:+03d}'];y=ours({**TRI,'pan':2,'amplfo':dd},[(0.05,72,0.8)],1.0,stereo=True)
    for ch,lab in ((0,'L'),(1,'R')):
        a=cyc(x[ch],P72,0.05+LAT+0.2)[1].max()/0.1098;b=cyc(y[ch],P72,0.05+LAT+0.2)[1].max()/cref
        if a>0.01:row(f'manual pan {dd} {lab} vs centre',a,b,'x')
from xm import spec,seg
for v in (16,64,127):
    a=spec(seg(d[f'xm_{v:03d}']));b=spec(seg(ours({**TRI,'osc2.wave':2,'osc2.range':7,'xmod':v},[(0.05,60,0.9)],1.1)))
    rows.append((f'xmod {v} spectrum',0,0));print(f'xmod {v} band spectrum rms difference {np.sqrt(np.mean((np.maximum(a,-60)-np.maximum(b,-60))**2)):.2f} dB')
f1=440*2**(-9/12);f2=f1*2**(7/12)
for b in (-32,0,32):
    x=seg(d[f'bal_{b:+03d}']);y=seg(ours({**TRI,'osc2.wave':2,'osc2.range':7,'balance':b},[(0.05,60,0.9)],1.1));x0=seg(d['bal_-64']);y0=seg(ours({**TRI,'osc2.wave':2,'osc2.range':7},[(0.05,60,0.9)],1.1))
    row(f'balance {b} OSC 1 vs alone',abs(tone(x,f1)[0])/abs(tone(x0,f1)[0]),abs(tone(y,f1)[0])/abs(tone(y0,f1)[0]),'x')
import nz2
NZ={'osc1.wave':2}
for k,sets,n in (('nz_c064',{'osc1.ctrl1':64,'osc1.ctrl2':0},60),('nz_c096',{'osc1.ctrl1':96,'osc1.ctrl2':0},60),('nz_c064_r112_n72',{'osc1.ctrl1':64,'osc1.ctrl2':112},72),('nz_c064_r096',{'osc1.ctrl1':64,'osc1.ctrl2':96},60)):
    y=ours({**NZ,**sets},[(0.05,n,1.3)],1.5);a=nz2.fitd(k);nz2.d={'tmp':y};b=nz2.fitd('tmp');nz2.d=np.load('cap6.npz')
    lvl=lambda x:20*np.log10(x[int((0.05+LAT+0.2)*SR):int((0.05+LAT+1.2)*SR)].std())
    row(f'noise {k[3:]} coefficient',a[0],b[0],'');row(f'noise {k[3:]} damping',a[1],b[1],'');print(f'   level emulator {lvl(d[k]):.2f} dB, ours {lvl(y):.2f} dB, diff {lvl(y)-lvl(d[k]):+.2f}')
g=np.array([abs(u/e-1) for _,e,u in rows if e]);print('median gap',round(100*np.median(g),1),'% worst',round(100*g.max(),1),'%')
