import numpy as np;from an3 import *
from scipy.optimize import least_squares
from nz import psd,bands,d
def lp(f,coef,q):
    zi=np.exp(-2j*np.pi*f/SR);return coef**2*zi/((1-zi)**2+coef**2*zi+coef*q*zi*(1-zi))
def fitd(k):
    fr,S=psd(d[k]);c,L=bands(fr,S);m=np.isfinite(L)&(L>np.nanmax(L)-60)
    r=lambda p:20*np.log10(np.abs(lp(c[m],np.exp(p[0]),np.exp(p[1]))**2)+1e-12)+p[2]-L[m]
    best=None
    for f0 in np.geomspace(0.003,1.5,25):
        for q0 in (0.02,0.1,0.5,1.0):
            try:s=least_squares(r,[np.log(f0),np.log(q0),np.nanmax(L)])
            except Exception:continue
            if best is None or s.cost<best.cost:best=s
    return np.exp(best.x[0]),np.exp(best.x[1]),np.sqrt(2*best.cost/m.sum()),best.x[2]
if __name__=='__main__':
    for k in [f'nz_c{c:03d}' for c in range(48,128,8)]+['nz_c064_r032','nz_c064_r096','nz_c064_r112','nz_c064_r112_n48','nz_c064_r112_n72','nz_c064_r112_n84']:
        coef,q,res,g=fitd(k);c=int(k[4:7]);n=int(k.split('_n')[1]) if '_n' in k else 60
        hz=440*2**((n-69)/12)*2**((c-64)/12)
        print(k,'coef',round(coef,5),'q',round(q,4),'res dB',round(res,2),'gain',round(g,1),' coef/(2pi hz/fs)',round(coef/(2*np.pi*hz/SR),4),flush=True)
