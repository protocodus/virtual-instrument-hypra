import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jh;jh.FORCE=True
F={'a_osc_waveform':'FEEDBACK OSC'}
J=[(f'fb_c{c:03d}_a064',{**F,'a_osc1_ctrl_1':c,'a_osc1_ctrl_2':64},[(0.05,48,100,1.4)],1.6) for c in range(0,128,4)]
J+=[(f'fb_c{c:03d}_a127',{**F,'a_osc1_ctrl_1':c,'a_osc1_ctrl_2':127},[(0.05,48,100,1.4)],1.6) for c in range(0,128,8)]
J+=[(f'fb_c064_a064_n{n}',{**F,'a_osc1_ctrl_1':64,'a_osc1_ctrl_2':64},[(0.05,n,100,1.4)],1.6) for n in (36,42,54,60,66,72,84)]
jh.capture(J,'cap10.npz')
