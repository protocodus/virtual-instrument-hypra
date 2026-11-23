import numpy as np;from an3 import *
from scipy.optimize import least_squares
d=np.load('cap6.npz');lat=0.0957
def psd(x):
    o=int((0.05+lat+0.2)*SR);x=x[o:o+int(1.0*SR)];n=8192;w=np.hanning(n);S=np.mean([np.abs(np.fft.rfft(x[i:i+n]*w))**2 for i in range(0,len(x)-n,n//2)],0);return np.fft.rfftfreq(n,1/SR),S
def bands(f,S):
    c=np.geomspace(110,30000,110);v=np.array([S[(f>=a/1.03)&(f<a*1.03)].mean() for a in c]);return c,10*np.log10(v+1e-30)
z=lambda f:np.exp(-2j*np.pi*f/SR)
def svf_resp(f,fc,q):
    # Chamberlin-like analog prototype: LP and BP 2-pole
    s=1j*f/fc;den=s*s+s/q+1;return 1/den, (s/q)/den
def model(p,f,kind):
    fc,q,g=np.exp(p[0]),np.exp(p[1]),p[2]
    lp,bp=svf_resp(f,fc,q)
    H={'lplp':lp*lp,'lpbp':lp*bp,'bpbp':bp*bp}[kind]
    return 20*np.log10(np.abs(H)+1e-12)+g
def fit(k,kind):
    f,S=psd(d[k]);c,L=bands(f,S);m=np.isfinite(L)&(L>np.nanmax(L)-60)
    r=lambda p:model(p,c[m],kind)-L[m]
    best=None
    for fc0 in np.geomspace(50,15000,25):
        for q0 in (0.5,1,3,10):
            s=least_squares(r,[np.log(fc0),np.log(q0),np.nanmax(L)])
            if best is None or s.cost<best.cost:best=s
    return np.exp(best.x[0]),np.exp(best.x[1]),np.sqrt(2*best.cost/m.sum())
if __name__=='__main__':
    for k in ('nz_c064','nz_c064_r032','nz_c064_r096','nz_c064_r112','nz_c064_r112_n84','nz_c096'):
        print(k,*[(kind,*[round(v,2) for v in fit(k,kind)]) for kind in ('lplp','lpbp','bpbp')])
