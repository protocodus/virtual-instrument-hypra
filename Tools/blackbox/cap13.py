import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jh;jh.FORCE=True
FC={'a_osc_waveform':'NOISE','a_osc1_ctrl_1':127,'a_osc1_ctrl_2':0,'a_flt_resonance':112,'a_flt_cutoff_freq':40,'a_flt_env_depth':40,'a_flt_env_decay':0}
TRI={'a_osc_waveform':'TRIANGLE','a_osc1_ctrl_1':0}
J=[('warm',TRI,[(0.05,60,100,0.3)],0.5)]
for s in (0,16,32,48,64,80,96,112,127):J.append((f'fs_{s:03d}',{**FC,'a_flt_env_sustain':s},[(0.05,60,100,0.3),(0.5,60,100,0.9)],1.6))
for s in (0,16,32,48,64,80,96,112,127):J.append((f'as_{s:03d}',{**TRI,'a_amp_env_decay':0,'a_amp_env_sustain':s},[(0.05,60,100,0.3),(0.5,60,100,0.9)],1.6))
for lv in (0,32,64,96,127):J.append((f'al_{lv:03d}',{**TRI,'a_amp_level':lv},[(0.05,60,100,0.6)],0.9))
jh.capture(J,os.environ.get('OUT','cap13.npz'))
