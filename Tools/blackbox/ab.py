import sys,os,subprocess; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from an import *
import os; H=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','build-dsp','TranceraRenderEvents');r=np.load('cap2.npz')
def ours(sets,note,dur):
    ev=['0 node digital','0 set balance -64','0 set amp.a 0','0 set amp.s 127','0 set fenv.s 127','0 set velocityamp 0','0 set level 100','0 set filter.steep 0']+[f'0 set {k} {v}' for k,v in sets.items()]+[f'0.05 note {note} 100',f'{dur} off {note}']
    open('ev.txt','w').write('\n'.join(ev)+'\n');subprocess.run([H,'ev.txt','o.f32',str(SR),str(dur+.3)],check=True);return np.fromfile('o.f32','<f4').astype(float)
P={'osc1.wave':5,'osc1.ctrl1':32}
items=[('saw_000',{'osc1.wave':5,'osc1.ctrl1':0},48),('saw_032',P,48),('saw_064',{'osc1.wave':5,'osc1.ctrl1':64},48),('saw_127',{'osc1.wave':5,'osc1.ctrl1':127},48),
 ('sqr_000',{'osc1.wave':4,'osc1.ctrl1':0},48),('sqr_096',{'osc1.wave':4,'osc1.ctrl1':96},48),('tmod_032',{'osc1.wave':1,'osc1.ctrl1':32},48),('tmod_096',{'osc1.wave':1,'osc1.ctrl1':96},48),
 ('lp12_c064_r000',{**P,'cutoff':64},24),('lp12_c064_r096',{**P,'cutoff':64,'resonance':96},24),('lp24_c064_r064',{**P,'filter.steep':1,'cutoff':64,'resonance':64},24),('lp24_c088_r096',{**P,'filter.steep':1,'cutoff':88,'resonance':96},24),
 ('hp12_c064_r064',{**P,'filter.type':0,'cutoff':64,'resonance':64},24),('bp24_c064_r000',{**P,'filter.type':1,'filter.steep':1,'cutoff':64},24)]
def cut(x):
    on=np.argmax(abs(x)>2e-4)+int(.08*SR);y=x[on:on+int(.62*SR)].copy();f=np.linspace(0,1,882);y[:882]*=f;y[-882:]*=f[::-1];return y
g=None;out=[];gap=np.zeros(int(.12*SR));worst=0
for name,sets,note in items:
    a=cut(r[name]);b=cut(ours(sets,note,0.9))
    if g is None:g=cut(r['saw_032']).std()/cut(ours(P,48,0.9)).std()
    b=b*g;worst=max(worst,abs(20*np.log10(b.std()/a.std())));out+=[a,gap,b,gap,gap]
    print(f'{name:16s} level ours-emulator {20*np.log10(b.std()/a.std()):+.2f} dB')
x=np.concatenate(out);x*=.5/abs(x).max()
subprocess.run(['ffmpeg','-v','error','-y','-f','f32le','-ar',str(SR),'-ac','1','-i','-','-ar','48000','-c:a','pcm_s24le',sys.argv[1]],input=x.astype('<f4').tobytes(),check=True)
print('one gain for all, set on the first item; worst level gap',round(worst,2),'dB; seconds',len(x)/SR)
