import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jh,mido,json
KEYS=['a_patchname','a_osc_waveform','a_osc1_ctrl_1','a_osc1_ctrl_2','a_flt_cutoff_freq','a_flt_resonance','a_amp_env_attack','a_chorus_type','a_delay_level','a_lfo1_rate']
def work():
    out={}
    jh.paced([],1.0)
    for prog in (0,1,2,10):
        jh.paced([mido.Message('program_change',program=prog,time=0)],1.0)
        out[prog]={k:str(jh.p.parameters[k].string_value) if hasattr(jh.p.parameters[k],'string_value') else str(jh.p.parameters[k].raw_value) for k in jh.p.parameters if k.startswith(('a_patchname','key_mode','a_osc_waveform','a_osc1_ctrl','a_flt_cutoff','a_chorus_type','a_delay_level'))}
    return out
print(json.dumps(jh.session(work),indent=0)[:3000])
