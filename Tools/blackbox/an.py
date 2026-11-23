import numpy as np
SR=88200
def seg(x,a=.45,b=.95):
    on=np.argmax(abs(x)>1e-4);end=len(x)-np.argmax(abs(x[::-1])>1e-4);return x[on+int((end-on)*a):on+int((end-on)*b)]
def amp(x,f):
    f=np.atleast_1d(f);N=len(x);w=np.hanning(N);t=np.arange(N)/SR
    return np.array([2*np.sum(x*w*np.exp(-2j*np.pi*q*t))/w.sum() for q in f])
def pitch(x,guess):
    f=guess
    for hmax in (2,5,12,30):
        N=len(x)//2;d=len(x)-N;e=[];w=[]
        for n in range(max(1,hmax//2),hmax+1):
            a=amp(x[:N],n*f)[0];b=amp(x[d:],n*f)[0]*np.exp(-2j*np.pi*n*f*d/SR)
            e.append(np.angle(b/a)/(2*np.pi*n*d/SR));w.append(n*n*abs(a))
        f+=np.average(e,weights=w)
    return f
def harm(x,f,top=20000):
    n=np.arange(1,int(top/f)+1);return n,amp(x,n*f)
def cycle(x,f,points=512,cycles=40):
    """One averaged cycle, resampled, starting at the steepest fall."""
    P=SR/f;t=np.arange(len(x));ph=(t/P)%1;idx=(ph*points).astype(int)
    s=np.bincount(idx,x,points)/np.maximum(np.bincount(idx,minlength=points),1)
    k=np.argmin(np.diff(np.r_[s,s[0]]));return np.roll(s,-(k+1))
mhz=lambda m:440*2**((m-69)/12)
