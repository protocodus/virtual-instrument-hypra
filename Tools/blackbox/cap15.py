import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jh;jh.FORCE=True
TRI={'a_osc_waveform':'TRIANGLE','a_osc1_ctrl_1':0}
T=['SUPER CHORUS SLW','SUPER CHORUS MID','SUPER CHORUS FST','SUPER CHORUS CLR','FLANGER SLOW','FLANGER DEEP','FLANGER FAST','DEEP PHASING SLW','JET PHASING','TWISTING','FREEZE PHASE 1','FREEZE PHASE 2']
J=[('warm',TRI,[(0.05,60,100,0.3)],0.5)]
for i,ty in enumerate(T):
    for lv in (0,64,127):J.append((f'st_{i:02d}_{lv:03d}',{**TRI,'a_chorus_type':ty,'a_chorus_level':lv,'a_multi_fx_level':lv},[(0.05,60,100,1.6)],1.8))
jh.capture(J,os.environ['OUT'])
