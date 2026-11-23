import numpy as np;from an3 import *
d=np.load('cap6.npz');lat=0.0957
tri=lambda p:1-4*np.abs(((p+0.25)%1)-0.5)
def spec(x,top=12000):
    n=len(x);S=np.abs(np.fft.rfft(x*np.blackman(n)))**2;f=np.fft.rfftfreq(n,1/SR);b=np.arange(0,top,25)
    e=np.array([S[(f>=a)&(f<a+25)].sum() for a in b]);return 10*np.log10(e/e.sum()+1e-12)
def seg(x):o=int((0.05+lat+0.2)*SR);return x[o:o+int(0.6*SR)]
def synth(f1,f2,beta,n=int(0.6*SR),ph=0.0):
    t=np.arange(n)/SR;m=tri(f2*t+ph);inst=f1*(1+beta*m);p=np.cumsum(inst)/SR;return tri(p)
f1=440*2**(-9/12);f2=f1*2**(7/12)
def fit(x,f1,f2):
    S=spec(seg(x));best=None
    for beta in np.r_[0:0.2:0.005,0.2:3:0.02]:
        e=np.mean([np.mean((np.maximum(spec(synth(f1,f2,beta,ph=ph)),-70)-np.maximum(S,-70))**2) for ph in (0,0.37)])
        if best is None or e<best[0]:best=(e,beta)
    return best
if __name__=='__main__':
    for v in list(range(0,128,16))+[127]:
        e,b=fit(d[f'xm_{v:03d}'],f1,f2);print(v,'beta',round(b,3),'err',round(e,1),flush=True)
    for n in (48,72):
        g1=440*2**((n-69)/12);e,b=fit(d[f'xm_064_n{n}'],g1,g1*2**(7/12));print('n',n,'beta',round(b,3),'err',round(e,1))
    e,b=fit(d['xm_064_r12'],f1,2*f1);print('ratio 2 beta',round(b,3),'err',round(e,1))
