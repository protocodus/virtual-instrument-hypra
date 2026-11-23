"""One factory program on JE-8086 and Trancera with settings overridden one at a time.
usage: variants.py PROG OUTDIR 'label:param=value,param=value;label:...'  (values are the knob value;
bipolar controls take -64..63). Captures one held note (57, 2 s) per variant on both."""
import sys,os,json,subprocess; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
try:
    import mido
except ImportError:
    mido=None
PROG=int(sys.argv[1]);OUT=sys.argv[2];SPEC=sys.argv[3];NOTE=int(os.environ.get('NOTE','57'))
VAR=[(s.split(':')[0],[kv.split('=') for kv in s.split(':')[1].split(',') if kv]) for s in SPEC.split(';')]
SR=88200;LAT=0.0957
def capture():
    import jh
    def work():
        res={};jh.paced([],1.0)
        for label,ov in VAR:
            jh.paced([mido.Message('program_change',program=PROG,time=0)],0.9)
            params={}
            for k,q in jh.p.parameters.items():
                if k.startswith('a_'):
                    vv=[str(v) for v in q.valid_values];params[k]=[int(round(q.raw_value*(len(vv)-1))),len(vv),str(q.string_value)]
            for k,v in (('key_mode','SINGLE'),('arp',False)):jh.setp(k,v,True)
            for k in ('a_velocity','a_active_vel_asgn','a_active_ctrl_asgn'):jh.setp(k,False,True)
            for k,v in ov:
                jh.setp(k,float(v),True);q=jh.p.parameters[k];vv=[str(x) for x in q.valid_values]
                params[k]=[int(round(q.raw_value*(len(vv)-1))),len(vv),str(q.string_value)]
            jh._cur.clear();jh.paced([mido.Message('control_change',control=120,value=0,time=0)],0.4)
            y=jh.play([(0.05,NOTE,100,2.0)],2.6);np.save(f'{OUT}/v_{label}.npy',y.astype('f4'));res[label]=params
        return res
    json.dump(jh.session(work),open(f'{OUT}/variants.json','w'))
def compare():
    import presets_map as m
    H=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','build-dsp','TranceraRenderEvents')
    d=json.load(open(f'{OUT}/variants.json'))
    B=[63*2**(k/2) for k in range(0,17)]
    def bands(x):
        y=x.mean(0)[int((0.05+LAT+0.4)*SR):int((0.05+LAT+1.9)*SR)];n=8192;w=np.hanning(n);S=np.mean([np.abs(np.fft.rfft(y[i:i+n]*w))**2 for i in range(0,len(y)-n,n//2)],0);f=np.fft.rfftfreq(n,1/SR)
        return np.array([10*np.log10(S[(f>=a/2**0.25)&(f<a*2**0.25)].sum()+1e-20) for a in B])
    S=slice(None) if os.environ.get('FULL') else slice(None,None,2)
    print('band (Hz)        ',' '.join(f'{int(b):5d}' for b in B[S]))
    for label,params in d.items():
        a=np.load(f'{OUT}/v_{label}.npy').astype(float)
        ev=['0 node digital']+m.events(params)+[f'0.05 note {NOTE} 100',f'2.05 off {NOTE}']
        open(f'{OUT}/ev.txt','w').write('\n'.join(ev)+'\n');subprocess.run([H,f'{OUT}/ev.txt',f'{OUT}/o.f32',str(SR),'2.6','stereo'],check=True)
        b=np.fromfile(f'{OUT}/o.f32','<f4').astype(float).reshape(-1,2).T;b=np.concatenate([np.zeros((2,int(LAT*SR))),b],1)
        ba=bands(a);bb=bands(b)
        print(f'{label:16s} emu ',' '.join(f'{x:5.0f}' for x in ba[S]));print(f'{"":16s} ours',' '.join(f'{x:5.0f}' for x in bb[S]))
if __name__=='__main__':
    os.makedirs(OUT,exist_ok=True)
    if os.environ.get('COMPARE'):compare()
    else:capture()
