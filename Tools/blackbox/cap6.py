import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np,jh
J=[];CC={}
TRI={'a_osc_waveform':'TRIANGLE'}
# short attacks and the late starts, on a high note for fine resolution
for v in range(0,42,2):J.append((f'sa_{v:03d}',{**TRI,'a_amp_env_attack':v},[(0.05,84,100,0.5)],0.7))
for v in (48,64,96):J.append((f'sa_{v:03d}',{**TRI,'a_amp_env_attack':v},[(0.05,84,100,1.2)],1.4))
for v in (16,32,64,96):J.append((f'sr_{v:03d}',{**TRI,'a_amp_env_release':v},[(0.05,84,100,0.3)],1.0))
for v in (0,8,16,24,32):J.append((f'sd_{v:03d}',{**TRI,'a_amp_env_sustain':0,'a_amp_env_decay':v},[(0.05,84,100,0.2),(0.4,84,100,0.4)],1.0))
# LFO 1 fade and waveforms
for v in range(0,128,16):J.append((f'fa_{v:03d}',{**TRI,'a_osc_lfo1_depth':24,'a_lfo1_rate':96,'a_lfo1_fade':v},[(0.05,72,100,6.0)],6.2))
J.append(('fa_127',{**TRI,'a_osc_lfo1_depth':24,'a_lfo1_rate':96,'a_lfo1_fade':127},[(0.05,72,100,9.0)],9.2))
for w in ('SAWTOOTH','SQUARE','RANDOM'):J.append((f'lw_{w.lower()}',{**TRI,'a_osc_lfo1_depth':24,'a_lfo1_rate':64,'a_lfo1_waveform':w},[(0.05,72,100,2.5)],2.7))
J.append(('lw_triangle',{**TRI,'a_osc_lfo1_depth':24,'a_lfo1_rate':64},[(0.05,72,100,2.5)],2.7))
# LFO 1 to amp and pan
for d in (-64,-32,32,63):J.append((f'st_al_{d:+03d}',{**TRI,'a_amp_lfo1_depth':d,'a_lfo1_rate':64,'a_lfo1_mod':'OFF'},[(0.05,72,100,1.5)],1.7))
for d in (-64,0,63):J.append((f'st_pan_auto_{d:+03d}',{**TRI,'a_amp_lfo1_depth':d,'a_lfo1_rate':64,'a_lfo1_mod':'AUTO'},[(0.05,72,100,1.5)],1.7))
for d in (-64,-32,0,32,63):J.append((f'st_pan_man_{d:+03d}',{**TRI,'a_amp_lfo1_depth':d,'a_lfo1_mod':'MANUAL'},[(0.05,72,100,0.8)],1.0))
# cross mod and ring, osc 2 a fifth up
X={**TRI,'a_osc2_waveform':'TRIANGLE','a_osc2_range':7}
for v in range(0,128,16):J.append((f'xm_{v:03d}',{**X,'a_cross_mod_depth':v},[(0.05,60,100,0.9)],1.1))
J.append(('xm_127',{**X,'a_cross_mod_depth':127},[(0.05,60,100,0.9)],1.1))
for n in (48,72):J.append((f'xm_064_n{n}',{**X,'a_cross_mod_depth':64},[(0.05,n,100,0.9)],1.1))
J.append(('xm_064_r12',{**X,'a_osc2_range':12,'a_cross_mod_depth':64},[(0.05,60,100,0.9)],1.1))
J.append(('ring',{**X,'a_ring_modulator':True},[(0.05,60,100,0.9)],1.1))
for b in (-64,-32,0,32,63):J.append((f'bal_{b:+03d}',{**X,'a_osc_balance':b},[(0.05,60,100,0.9)],1.1))
# noise oscillator: its own filter, dense, and key tracking
for c in range(0,128,8):J.append((f'nz_c{c:03d}',{'a_osc_waveform':'NOISE','a_osc1_ctrl_1':c,'a_osc1_ctrl_2':0},[(0.05,60,100,1.3)],1.5))
for r in (32,96,112):J.append((f'nz_c064_r{r:03d}',{'a_osc_waveform':'NOISE','a_osc1_ctrl_1':64,'a_osc1_ctrl_2':r},[(0.05,60,100,1.3)],1.5))
for n in (36,48,72,84):J.append((f'nz_c064_r112_n{n}',{'a_osc_waveform':'NOISE','a_osc1_ctrl_1':64,'a_osc1_ctrl_2':112},[(0.05,n,100,1.3)],1.5))
# velocity
FC={'a_osc_waveform':'NOISE','a_osc1_ctrl_1':127,'a_osc1_ctrl_2':0,'a_flt_resonance':112,'a_flt_cutoff_freq':40,'a_flt_env_depth':40}
for vel in (1,32,64,100,127):
    J.append((f'va_on_{vel:03d}',{**TRI,'a_velocity':True},[(0.05,72,vel,0.6)],0.8))
    J.append((f'va_off_{vel:03d}',{**TRI,'a_velocity':False},[(0.05,72,vel,0.6)],0.8))
    J.append((f'vf_on_{vel:03d}',{**FC,'a_velocity':True},[(0.05,60,vel,0.9)],1.1))
# LFO 2 under the modulation lever (last, CC1 stays up)
for v in range(0,128,8):
    L=float(np.clip(2.5/(0.05*500**(v/127)),1.5,12));n=f'l2r_{v:03d}';J.append((n,{**TRI,'a_pitch_lfo2_depth':24,'a_lfo2_rate':v},[(0.05,72,100,L)],L+0.2));CC[n]=[(0.0,1,127)]
J.append(('l2r_127',{**TRI,'a_pitch_lfo2_depth':24,'a_lfo2_rate':127},[(0.05,72,100,1.5)],1.7));CC['l2r_127']=[(0.0,1,127)]
for d in (8,16,32,48,63,-32):n=f'l2d_{d:+03d}';J.append((n,{**TRI,'a_pitch_lfo2_depth':d,'a_lfo2_rate':80},[(0.05,72,100,2.0)],2.2));CC[n]=[(0.0,1,127)]
for d in (16,32,63):n=f'l2f_{d:+03d}';J.append((n,{**FC,'a_flt_env_depth':0,'a_flt_cutoff_freq':64,'a_flt_lfo2_depth':d,'a_lfo2_rate':64},[(0.05,60,100,2.5)],2.7));CC[n]=[(0.0,1,127)]
for d in (32,63):n=f'l2a_{d:+03d}';J.append((n,{**TRI,'a_amp_lfo2_depth':d,'a_lfo2_rate':64},[(0.05,72,100,2.0)],2.2));CC[n]=[(0.0,1,127)]
for m in (32,64,96):n=f'l2m_{m:03d}';J.append((n,{**TRI,'a_pitch_lfo2_depth':63,'a_lfo2_rate':80},[(0.05,72,100,2.0)],2.2));CC[n]=[(0.0,1,m)]
print(len(J),'jobs,',round(sum(j[3]+0.4 for j in J)/60,1),'min',flush=True)
if 'dry' not in sys.argv:jh.capture(J,'cap6.npz',CC)
