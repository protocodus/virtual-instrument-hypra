import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jh
J=[];S={'a_osc_waveform':'SUPER SAW'}
P=[0,7,15,23,31,39,47,55,63,71,79,87,95,103,111,119,127]
for v in P:J.append((f'sd_{v:03d}',{**S,'a_osc1_ctrl_1':v,'a_osc1_ctrl_2':64},[(0.05,72,100,3.6)],3.8))
for v in P:J.append((f'sm_{v:03d}',{**S,'a_osc1_ctrl_1':64,'a_osc1_ctrl_2':v},[(0.05,72,100,3.6)],3.8))
X={'a_osc_waveform':'TRIANGLE','a_osc1_ctrl_1':0,'a_osc2_waveform':'TRIANGLE','a_osc2_range':7,'a_ring_modulator':True}
for b in (0,63):J.append((f'ring_b{b:+03d}',{**X,'a_osc_balance':b},[(0.05,60,100,0.9)],1.1))
J.append(('ring_saw_b+63',{**X,'a_osc_waveform':'SAW','a_osc2_waveform':'SAW','a_osc_balance':63},[(0.05,60,100,0.9)],1.1))
F={'a_osc_waveform':'FEEDBACK OSC'}
for c1 in (0,32,64,96,127):
    for c2 in (0,64,127):J.append((f'fb_c{c1:03d}_a{c2:03d}',{**F,'a_osc1_ctrl_1':c1,'a_osc1_ctrl_2':c2},[(0.05,48,100,1.8)],2.0))
for n in (36,60,72):J.append((f'fb_c064_a064_n{n}',{**F,'a_osc1_ctrl_1':64,'a_osc1_ctrl_2':64},[(0.05,n,100,1.8)],2.0))
print(len(J),'jobs,',round(sum(j[3]+0.4 for j in J)/60,1),'min',flush=True)
if 'dry' not in sys.argv:jh.capture(J,'cap7.npz')
