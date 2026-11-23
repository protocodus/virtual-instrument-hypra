import numpy as np
SR=88200;LAT=0.0957
names=['SUPER CHORUS SLW','SUPER CHORUS MID','SUPER CHORUS FST','SUPER CHORUS CLR','FLANGER SLOW','FLANGER DEEP','FLANGER FAST','DEEP PHASING SLW','JET PHASING','TWISTING','FREEZE PHASE 1','FREEZE PHASE 2']
f0=440*2**(3/12)
def modrate(x):
    o=int((0.05+LAT+0.2)*SR);y=x[o:o+int(2.7*SR)];hop=int(0.01*SR);n=int(0.04*SR);t=np.arange(n)/SR;w=np.hanning(n)
    best=(0,None)
    for h in (1,3,5,7,9,11):
        z=np.exp(-2j*np.pi*h*f0*t)*w;env=np.array([abs(np.dot(y[i:i+n],z)) for i in range(0,len(y)-n,hop)]);env=20*np.log10(env+1e-9)
        if env.std()>best[0]:best=(env.std(),env)
    e=best[1]-best[1].mean();N=len(e)*16;F=np.abs(np.fft.rfft(e*np.hanning(len(e)),N));fr=np.fft.rfftfreq(N,0.01);m=fr>0.05
    return fr[m][np.argmax(F[m])],best[0]
def lags(x):
    o=int((0.05+LAT+0.2)*SR);y=x[o:o+int(1.6*SR)];y=y-y.mean();n=len(y);F=np.fft.rfft(y,2*n);ac=np.fft.irfft(np.abs(F)**2)[:int(0.03*SR)];ac/=ac[0]
    i0=int(0.0003*SR);j=i0+np.argmax(ac[i0:]);return j/SR*1000,ac[j]
def run(d,prefix='st_'):
    for i,nm in enumerate(names):
        r,dev=modrate(d[f'{prefix}ch_{i:02d}'][0]);lL=lags(d[f'{prefix}chn_{i:02d}'][0]);lR=lags(d[f'{prefix}chn_{i:02d}'][1])
        print(f'{nm:18s} mod {r:5.2f} Hz (swing {dev:4.1f} dB)  noise comb L {lL[0]:5.2f} ms ({lL[1]:+.2f})  R {lR[0]:5.2f} ms ({lR[1]:+.2f})',flush=True)
if __name__=='__main__':run(np.load('cap11.npz'))
