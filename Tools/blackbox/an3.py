import numpy as np
SR=88200
def onset(x,th=1e-4):return int(np.argmax(abs(x)>th))
def rms_env(x,win=0.008,hop=0.001):
    w=int(win*SR);h=int(hop*SR);c=np.cumsum(np.r_[0,x.astype('f8')**2])
    i=np.arange(0,len(x)-w,h);return (i+w/2)/SR,np.sqrt((c[i+w]-c[i])/w)
def zc_pitch(x):
    """Instantaneous frequency from rising zero crossings: (times, Hz)."""
    s=np.signbit(x);i=np.where(s[:-1]&~s[1:])[0];t=i+x[i]/(x[i]-x[i+1])
    return (t[1:]+t[:-1])/2/SR,SR/np.diff(t)
def fc_track(x,n=4096,hop=1024):
    w=np.hanning(n);f=np.fft.rfftfreq(n,1/SR);out=[];ts=[]
    k=np.ones(5)/5
    for i in range(0,len(x)-n,hop):
        S=np.convolve(np.abs(np.fft.rfft(x[i:i+n]*w))**2,k,'same');S[:2]=0
        j=int(np.argmax(S))
        if 0<j<len(S)-1:
            a,b,c=np.log(S[j-1:j+2]+1e-30);d=0.5*(a-c)/(a-2*b+c)
        else:d=0
        out.append((j+d)*SR/n);ts.append((i+n/2)/SR)
    return np.array(ts),np.array(out)
def fc_static(x):
    o=onset(x);seg=x[o+int(0.25*SR):o+int(0.85*SR)];t,f=fc_track(seg,8192,2048);return float(np.median(f))
def knob(fc):
    coef=2*np.sin(np.pi*np.asarray(fc)/SR);return 64+12*np.log2(coef/0.039016)
