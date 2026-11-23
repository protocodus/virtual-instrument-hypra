import sys,os,json; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jh,mido
def sv(q):
    return str(q.string_value) if hasattr(q,'string_value') else str(q.raw_value)
def work():
    out={};jh.paced([],1.0)
    for prog in range(int(sys.argv[1]),int(sys.argv[2])):
        jh.paced([mido.Message('program_change',program=prog,time=0)],0.8)
        d={k:sv(q) for k,q in jh.p.parameters.items() if k.startswith('a_') or k in('key_mode',)}
        name=''.join(chr(int(float(d.get(f'a_patchname{i}','32')))) for i in range(1,17)).strip()
        out[prog]={'name':name,'params':d}
    return out
r=jh.session(work);json.dump(r,open(sys.argv[3],'w'),indent=0)
for k,v in r.items():print(k,v['name'],v['params']['a_osc_waveform'],v['params']['a_mono'],v['params']['a_chorus_type'],v['params']['key_mode'])
