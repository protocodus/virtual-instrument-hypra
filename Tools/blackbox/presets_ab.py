"""Factory patches on JE-8086 against Trancera: read each program's part A settings,
play the same phrase on both, save captures and the settings."""
import sys,os,json; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np,mido,jh
PROGS=[int(x) for x in sys.argv[1].split(',')];OUT=sys.argv[2]
def phrase(shift):
    n=[]
    for i,ch in enumerate(((57,60,64),(53,57,60),(48,52,55,60),(55,59,62))):
        n+=[(0.05+0.75*i,k+shift,100,0.7) for k in ch]
    for i,k in enumerate((69,72,76,74,72,69,67,69)):n.append((3.2+0.25*i,k+shift,100,0.22))
    return n
def work():
    res={};jh.paced([],1.0)
    for prog in PROGS:
        jh.paced([mido.Message('program_change',program=prog,time=0)],0.9)
        params={}
        for k,q in jh.p.parameters.items():
            if k.startswith('a_'):
                vv=[str(v) for v in q.valid_values];params[k]=[int(round(q.raw_value*(len(vv)-1))),len(vv),str(q.string_value)]
        name=''.join(chr(int(params[f'a_patchname{i}'][0])) if params[f'a_patchname{i}'][1]>2 else ' ' for i in range(1,17)).strip()
        for k,v in (('key_mode','SINGLE'),('arp',False)):jh.setp(k,v,True)
        if os.environ.get('PLAIN'):
            for k in ('a_velocity','a_active_vel_asgn','a_active_ctrl_asgn'):jh.setp(k,False,True)
        jh._cur.clear()
        jh.paced([mido.Message('control_change',control=120,value=0,time=0)],0.4)
        shift=-24 if any(w in name for w in ('Bass','303','Bs')) else 0
        notes=phrase(shift);y=jh.play(notes,6.5)
        res[str(prog)]={'name':name,'shift':shift,'params':params,'notes':notes}
        np.save(f'{OUT}/je{os.environ.get("PLAIN","")}_{prog:03d}.npy',y.astype('f4'));print(prog,name,flush=True)
    return res
r=jh.session(work);json.dump(r,open(f'{OUT}/presets{os.environ.get("PLAIN","")}.json','w'),indent=0)
