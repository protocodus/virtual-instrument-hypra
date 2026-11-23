import sys,numpy as np
src=open('ab.py').read().split("P={")[0];src=src.replace("r=np.load('cap2.npz')","r=np.load('cap7.npz')");exec(src)
from scipy.signal import find_peaks
f0=440*2**(3/12);P=[0,7,15,23,31,39,47,55,63,71,79,87,95,103,111,119,127]
SO=np.array([-0.11002313,-0.06288439,-0.01952356,0.0,0.01991221,0.06216538,0.10745242])
def spec(x):
    y=x[int(.4*SR):int(3.7*SR)];N=len(y)*4;S=np.abs(np.fft.rfft(y*np.blackman(len(y)),N));return np.fft.rfftfreq(N,1/SR),S
def spread(x,h):
    f,S=spec(x);m=(f>h*f0*0.8)&(f<h*f0*1.2);S=S[m];f=f[m];pk,_=find_peaks(S,distance=6);pk=pk[np.argsort(-S[pk])[:7]];fr=np.sort(f[pk])/(h*f0)-1
    return fr
def detune_amount(x):
    # best single scale d so that offsets = d*SO, using harmonic 3 for resolution
    fr=spread(x,3) if True else None
    if len(fr)<7:return np.nan
    return float(np.dot(fr-fr[3],SO)/np.dot(SO,SO))
off=None
def lev(x,offs,h):
    f,S=spec(x);return 20*np.log10(np.array([S[np.abs(f-h*f0*(1+o))<1.2].max() for o in offs]))
if __name__=='__main__':
    print('detune point: emulator amount / ours amount / ratio')
    for v in P:
        a=detune_amount(r[f'sd_{v:03d}']);b=detune_amount(ours({'osc1.wave':0,'osc1.ctrl1':v,'osc1.ctrl2':64},72,3.8))
        print(v,round(a,5),round(b,5),round(a/b,4) if b else None,flush=True)
    da=detune_amount(r['sm_063']);offs=da*SO
    print('mix point: centre-side (h1..h4 mean) emulator / ours')
    for v in P:
        la=np.mean([lev(r[f'sm_{v:03d}'],offs,h) for h in (1,2,3,4)],0);y=ours({'osc1.wave':0,'osc1.ctrl1':64,'osc1.ctrl2':v},72,3.8)
        ob=detune_amount(y)*SO;lb=np.mean([lev(y,ob,h) for h in (1,2,3,4)],0)
        print(v,round(la[3]-np.mean(np.delete(la,3)),2),round(lb[3]-np.mean(np.delete(lb,3)),2),' centre level emu/ours',round(la[3],2),round(lb[3],2),flush=True)
