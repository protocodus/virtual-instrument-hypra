import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np,jh
J=[];cl=lambda s,lo,hi:float(np.clip(s,lo,hi))
TRI={'a_osc_waveform':'TRIANGLE'}
FC={'a_osc_waveform':'NOISE','a_osc1_ctrl_1':127,'a_osc1_ctrl_2':0,'a_flt_resonance':112}
lr=lambda v:np.interp(v,[0,24,32,48,64,80,96,112,127],[0.08,0.12,0.39,1.11,3.22,5.42,9.1,15.4,50])
for v in range(0,128,4):L=cl(2.3/lr(v),1.5,26);J.append((f'lr_{v:03d}',{**TRI,'a_osc_lfo1_depth':24,'a_lfo1_rate':v},[(0.05,72,100,L)],L+0.2))
J.append(('lr_127',{**TRI,'a_osc_lfo1_depth':24,'a_lfo1_rate':127},[(0.05,72,100,1.5)],1.7))
at=lambda v:np.interp(v,[0,16,32,48,64,80,96,112,127],[0.004,0.03,0.11,0.23,0.47,0.69,1.4,3.6,8.8])
for v in list(range(0,128,8))+[127]:L=cl(1.6*at(v)+0.4,0.8,15);J.append((f'aa_{v:03d}',{**TRI,'a_amp_env_attack':v},[(0.05,60,100,L)],L+0.2))
rl=lambda v:np.interp(v,[0,16,32,48,64,80,96,112,127],[0.03,0.13,0.39,1.0,2.19,4.19,8.1,18.5,101])
for v in list(range(0,128,4))+[127]:L=cl(rl(v)/3+0.3,0.6,12);J.append((f'ar_{v:03d}',{**TRI,'a_amp_env_release':v},[(0.05,60,100,0.3)],L+0.4))
for dd in range(4,64,4):J.append((f'ld_d{dd:+03d}',{**TRI,'a_osc_lfo1_depth':dd,'a_lfo1_rate':80},[(0.05,72,100,2.5)],2.7))
J.append(('ld_d+63',{**TRI,'a_osc_lfo1_depth':63,'a_lfo1_rate':80},[(0.05,72,100,2.5)],2.7))
for dd in list(range(4,64,4))+[63]:J.append((f'pe_d{dd:+03d}',{**TRI,'a_pitch_env_depth':dd,'a_pitch_env_decay':64},[(0.05,72,100,1.6)],1.8))
for dd in (-8,-24,-48,-64):J.append((f'pe_d{dd:+03d}',{**TRI,'a_pitch_env_depth':dd,'a_pitch_env_decay':64},[(0.05,72,100,1.6)],1.8))
for dd in range(4,64,4):J.append((f'fe_d{dd:+03d}',{**FC,'a_flt_cutoff_freq':24 if dd<52 else 0,'a_flt_env_depth':dd},[(0.05,60,100,0.9)],1.1))
J.append(('fe_d+63',{**FC,'a_flt_cutoff_freq':0,'a_flt_env_depth':63},[(0.05,60,100,0.9)],1.1))
for dd in range(-8,-65,-8):J.append((f'fe_d{dd:+03d}',{**FC,'a_flt_cutoff_freq':127,'a_flt_env_depth':dd},[(0.05,60,100,0.9)],1.1))
for dd in range(4,44,4):J.append((f'lf_d{dd:+03d}',{**FC,'a_flt_cutoff_freq':64,'a_flt_lfo1_depth':dd,'a_lfo1_rate':48},[(0.05,60,100,2.8)],3.0))
for k in (-56,-48,-40,-24,-16,-8,8,16,24,40,48,56):
    for n in (36,84):J.append((f'kf_k{k:+03d}_n{n}',{**FC,'a_flt_cutoff_freq':64,'a_flt_key_follow':k},[(0.05,n,100,0.9)],1.1))
for v in (16,48,80,112):J.append((f'fr_{v:03d}',{**FC,'a_flt_cutoff_freq':40,'a_flt_env_depth':40,'a_amp_env_release':127,'a_flt_env_release':v},[(0.05,60,100,0.5)],cl(rl(v)/2+0.8,1.5,8)))
po=lambda v:np.interp(v,[0,16,32,48,64,80,96,112,127],[0.01,0.02,0.05,0.14,0.31,0.59,1.02,1.73,4])
P={**TRI,'a_mono':True,'a_portamento':True}
for v in list(range(0,128,16))+[127]:
    L=cl(2*po(v)+0.5,0.8,9);J.append((f'po24_{v:03d}',{**P,'a_portamento_time':v},[(0.05,48,100,0.7),(0.6,72,100,L)],L+0.8))
L=9;J.append(('po12_127',{**P,'a_portamento_time':127},[(0.05,48,100,0.7),(0.6,60,100,L)],L+0.8))
J.append(('po12d_064',{**P,'a_portamento_time':64},[(0.05,60,100,0.7),(0.6,48,100,1.5)],2.3))
J.append(('po05_064',{**P,'a_portamento_time':64},[(0.05,48,100,0.7),(0.6,53,100,1.5)],2.3))
print(len(J),'jobs,',round(sum(j[3]+0.4 for j in J)/60,1),'min',flush=True)
if 'dry' not in sys.argv:jh.capture(J,'cap4.npz')
