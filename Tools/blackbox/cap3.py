import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np,jh
J=[];u=lambda v:v/127
att=lambda v:0.0005*12000**u(v); dec=lambda v:0.004*3000**u(v)
T=[0,16,32,48,64,80,96,112,127]
cap=lambda s,lo=1.0,hi=9.0:float(np.clip(s,lo,hi))
TRI={'a_osc_waveform':'TRIANGLE'}
FC={'a_osc_waveform':'NOISE','a_osc1_ctrl_1':127,'a_osc1_ctrl_2':0,'a_flt_resonance':112}
for v in T:
    J.append((f'aa_{v:03d}',{**TRI,'a_amp_env_attack':v},[(0.05,60,100,cap(1.6*att(v)+0.3)) ],cap(1.6*att(v)+0.6)))
    J.append((f'ad_{v:03d}',{**TRI,'a_amp_env_sustain':0,'a_amp_env_decay':v},[(0.05,60,100,cap(1.5*dec(v)))],cap(1.5*dec(v))+0.2))
    J.append((f'ar_{v:03d}',{**TRI,'a_amp_env_release':v},[(0.05,60,100,0.5)],cap(1.5*dec(v)+0.6,1.0,9.5)))
for c in range(0,128,4):J.append((f'fc_c{c:03d}',{**FC,'a_flt_cutoff_freq':c},[(0.05,60,100,0.9)],1.1))
J.append(('fc_c127',{**FC,'a_flt_cutoff_freq':127},[(0.05,60,100,0.9)],1.1))
for k in (-64,-32,0,32,63):
    for n in (36,60,84):J.append((f'kf_k{k:+03d}_n{n}',{**FC,'a_flt_cutoff_freq':64,'a_flt_key_follow':k},[(0.05,n,100,0.9)],1.1))
for d in range(8,64,8):J.append((f'fe_d{d:+03d}',{**FC,'a_flt_cutoff_freq':40,'a_flt_env_depth':d},[(0.05,60,100,0.9)],1.1))
J.append(('fe_d+63',{**FC,'a_flt_cutoff_freq':40,'a_flt_env_depth':63},[(0.05,60,100,0.9)],1.1))
for d in (-16,-32,-48,-64):J.append((f'fe_d{d:+03d}',{**FC,'a_flt_cutoff_freq':110,'a_flt_env_depth':d},[(0.05,60,100,0.9)],1.1))
FE={**FC,'a_flt_cutoff_freq':40,'a_flt_env_depth':40}
for v in (16,48,80,112):
    J.append((f'fa_{v:03d}',{**FE,'a_flt_env_attack':v},[(0.05,60,100,cap(1.6*att(v)+0.3))],cap(1.6*att(v)+0.6)))
    J.append((f'fd_{v:03d}',{**FE,'a_flt_env_sustain':0,'a_flt_env_decay':v},[(0.05,60,100,cap(1.5*dec(v)))],cap(1.5*dec(v))+0.2))
    J.append((f'fr_{v:03d}',{**FE,'a_flt_env_release':v},[(0.05,60,100,0.5)],cap(1.5*dec(v)+0.6,1.0,9.5)))
lfo=lambda v:0.05*500**u(v)
for v in T:J.append((f'lr_{v:03d}',{**TRI,'a_osc_lfo1_depth':24,'a_lfo1_rate':v},[(0.05,72,100,cap(3/lfo(v),1.5,9.5))],cap(3/lfo(v),1.5,9.5)+0.2))
for d in (4,8,16,32,48,63,-32):J.append((f'ld_d{d:+03d}',{**TRI,'a_osc_lfo1_depth':d,'a_lfo1_rate':80},[(0.05,72,100,2.5)],2.7))
for d in (16,32,63):J.append((f'lf_d{d:+03d}',{**FC,'a_flt_cutoff_freq':64,'a_flt_lfo1_depth':d,'a_lfo1_rate':48},[(0.05,60,100,4.5)],4.7))
for d in (4,8,16,32,63,-32):J.append((f'pe_d{d:+03d}',{**TRI,'a_pitch_env_depth':d,'a_pitch_env_decay':64},[(0.05,72,100,1.6)],1.8))
for v in (0,32,64,96,127):J.append((f'pd_{v:03d}',{**TRI,'a_pitch_env_depth':32,'a_pitch_env_decay':v},[(0.05,72,100,cap(1.5*dec(v)))],cap(1.5*dec(v))+0.2))
for v in (32,64,96):J.append((f'pa_{v:03d}',{**TRI,'a_pitch_env_depth':32,'a_pitch_env_attack':v,'a_pitch_env_decay':127},[(0.05,72,100,cap(1.6*att(v)+0.4))],cap(1.6*att(v)+0.6)))
por=lambda v:0.004*750**u(v)
for v in T:J.append((f'po_{v:03d}',{**TRI,'a_mono':True,'a_portamento':True,'a_portamento_time':v},[(0.05,48,100,0.7),(0.6,60,100,cap(2*por(v)+0.4,0.8,7))],cap(2*por(v)+0.4,0.8,7)+0.8))
print(len(J),'jobs,',round(sum(j[3]+0.4 for j in J)/60,1),'min',flush=True)
if 'dry' not in sys.argv:jh.capture(J,'cap3.npz')
