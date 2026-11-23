"""Black-box JE-8086 harness: hosts the plugin with its editor open (it only
syncs patch edits while its message loop runs) and plays it in real time."""
import sys,os,time,threading,traceback,numpy as np,pedalboard,mido
SR=88200
p=pedalboard.load_plugin('/Library/Audio/Plug-Ins/VST3/JE8086.vst3')
def paced(msgs,dur,block=0.02):
    n=int(round(dur/block));slots=[[] for _ in range(n)]
    for m in msgs:
        i=min(n-1,max(0,int(m.time/block+1e-9)));slots[i].append(m.copy(time=max(0.0,m.time-i*block)))
    out=[]
    for now in slots:
        t0=time.time();out.append(p(now,duration=block,sample_rate=SR,num_channels=2,reset=False))
        time.sleep(max(0,block-(time.time()-t0)))
    return np.concatenate(out,axis=1)
def _index(q,value):
    vv=[str(v) for v in q.valid_values];n=len(vv)
    if isinstance(value,str):return vv.index(value),n
    if vv[0] in('False','True'):return vv.index(str(bool(value))),n
    try:return int(np.argmin([abs(float(v)-value) for v in vv])),n
    except ValueError:return int(value+n//2),n
_cur={}
def setp(name,value,force=False):
    if not force and _cur.get(name)==value:return 0
    q=p.parameters[name];i,n=_index(q,value)
    if force:
        q.raw_value=((i+1)%n)/(n-1);paced([],0.04)
    q.raw_value=i/(n-1);_cur[name]=value;return 1
NEUTRAL={'a_osc_waveform':'SAW','a_osc1_ctrl_1':0,'a_osc1_ctrl_2':0,'a_osc_balance':-64,'a_osc2_waveform':'SAW','a_osc2_range':0,'a_osc2_fine_wide':0,
 'a_lfo1_rate':64,'a_lfo2_rate':64,'a_flt_type':'LPF','a_cutoff_slope':'-12dB/oct','a_flt_cutoff_freq':127,'a_flt_env_sustain':127,
 'a_amp_level':100,'a_amp_env_sustain':127,'a_bend_range_up':2,'a_bend_range_down':2,'a_oscillator_shift':0}
def n_mid(vv):return len(vv) in(51,101,128) and vv[0].startswith(('-','L'))
def neutral():
    out={}
    for k,q in p.parameters.items():
        if not k.startswith('a_') or 'patchname' in k:continue
        vv=[str(v) for v in q.valid_values]
        if vv[0] in('False','True'):out[k]=False
        elif 'CENTER' in vv:out[k]='CENTER'
        elif '0' in vv:out[k]='0'
        elif '0.0' in vv:out[k]=0
        elif n_mid(vv):out[k]=vv[len(vv)//2]
        else:out[k]=vv[0]
    out.update(NEUTRAL);return out
_first=True
FORCE=False
def patch(params):
    """Sets part A to a neutral patch plus params; returns after it has settled."""
    global _first
    full=neutral();full.update(params);n=0
    if _first:
        for k,v in (('key_mode','SINGLE'),('arp',False),('panel_select',0),('part_detune',0),('individualtriggerswitch',False)):n+=setp(k,v,True)
    for k,v in full.items():
        n+=setp(k,v,_first or (FORCE and k in params and k!='a_osc_waveform'))
        if _first:paced([],0.02)
    _first=False
    paced([mido.Message('control_change',control=120,value=0,time=0)],0.25+0.03*n)
def play(notes,dur,ccs=()):
    msgs=[]
    for t,n,v,l in notes:msgs+=[mido.Message('note_on',note=n,velocity=v,time=t),mido.Message('note_off',note=n,velocity=0,time=t+l)]
    for t,c,v in ccs:msgs.append(mido.Message('control_change',control=c,value=v,time=t))
    return paced(msgs,dur)
def capture(jobs,path,ccs={}):
    """jobs: (name, params, notes, seconds). Saves the left channel of each."""
    def work():
        r={}
        for name,params,notes,dur in jobs:
            patch(params);y=play(notes,dur,ccs.get(name,()));r[name]=(y if name.startswith('st_') else y[0]).astype('f4')
        return r
    r=session(work);np.savez(path,**r);print('saved',len(r),'captures to',path)
def session(work):
    done=threading.Event();box={}
    def run():
        try:
            paced([],3.0);box['r']=work()
        except Exception:traceback.print_exc()
        done.set()
    threading.Thread(target=run,daemon=True).start();p.show_editor(done);return box.get('r')
