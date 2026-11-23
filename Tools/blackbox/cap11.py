import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jh;jh.FORCE=True
J=[]
NZ={'a_osc_waveform':'NOISE','a_osc1_ctrl_1':127,'a_osc1_ctrl_2':0}
for b,t in ((0,0),(-64,0),(-32,0),(32,0),(63,0),(0,-64),(0,-32),(0,32),(0,63)):
    J.append((f'st_tone_b{b:+03d}_t{t:+03d}',{**NZ,'a_tone_ctrl_bass':b,'a_tone_ctrl_treble':t},[(0.05,60,100,1.2)],1.4))
CL={'a_osc_waveform':'TRIANGLE','a_osc1_ctrl_1':0,'a_delay_level':127}
for ty in ('PANNING L->R','PANNING R->L','PANNING SHORT','MONO SHORT','MONO LONG'):
    for tm in (64,127):
        J.append((f'st_dl_{ty.replace(" ","").replace("->","")}_t{tm:03d}',{**CL,'a_delay_type':ty,'a_delay_time':tm,'a_delay_feedback':0},[(0.05,96,100,0.01)],2.9))
for tm in range(0,128,16):J.append((f'st_dlt_{tm:03d}',{**CL,'a_delay_type':'MONO LONG','a_delay_time':tm,'a_delay_feedback':0},[(0.05,96,100,0.01)],2.0))
for fb in (32,64,96,127):J.append((f'st_dlf_{fb:03d}',{**CL,'a_delay_type':'MONO SHORT','a_delay_time':64,'a_delay_feedback':fb},[(0.05,96,100,0.01)],3.0))
for lv in (32,64,96):J.append((f'st_dll_{lv:03d}',{**CL,'a_delay_type':'MONO SHORT','a_delay_time':64,'a_delay_feedback':0,'a_delay_level':lv},[(0.05,96,100,0.01)],1.0))
types=['SUPER CHORUS SLW','SUPER CHORUS MID','SUPER CHORUS FST','SUPER CHORUS CLR','FLANGER SLOW','FLANGER DEEP','FLANGER FAST','DEEP PHASING SLW','JET PHASING','TWISTING','FREEZE PHASE 1','FREEZE PHASE 2']
for i,ty in enumerate(types):
    J.append((f'st_ch_{i:02d}',{'a_osc_waveform':'TRIANGLE','a_osc1_ctrl_1':0,'a_chorus_type':ty,'a_chorus_level':127,'a_multi_fx_level':127},[(0.05,72,100,3.0)],3.2))
    J.append((f'st_chn_{i:02d}',{**NZ,'a_chorus_type':ty,'a_chorus_level':127,'a_multi_fx_level':127},[(0.05,60,100,2.0)],2.2))
print(len(J),'jobs',round(sum(j[3]+0.6 for j in J)/60,1),'min',flush=True)
jh.capture(J,'cap11.npz')
