import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jh;jh.FORCE=True
TRI={'a_osc_waveform':'TRIANGLE','a_osc1_ctrl_1':0}
J=[('warm',TRI,[(0.05,60,100,0.3)],0.5)]
for ty in ('SUPER CHORUS SLW','SUPER CHORUS FST','FLANGER SLOW'):
    for lv in (0,32,64,96,127):J.append((f'st_cl_{ty.replace(" ","")}_{lv:03d}',{**TRI,'a_chorus_type':ty,'a_chorus_level':lv,'a_multi_fx_level':lv},[(0.05,60,100,1.6)],1.8))
for lv in (0,32,64,96,127):J.append((f'st_dl_{lv:03d}',{**TRI,'a_delay_type':'MONO SHORT','a_delay_time':64,'a_delay_feedback':0,'a_delay_level':lv},[(0.05,96,100,0.01)],0.8))
jh.capture(J,os.environ['OUT'])
