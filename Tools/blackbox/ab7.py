import sys,os,subprocess; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('cmp4.py').read().split("rows=[]")[0])
c=np.load('cap11.npz')
NZ={'osc1.wave':2,'osc1.ctrl1':127}
CL={'osc1.wave':6,'osc1.ctrl1':0,'delay':127,'delay.feedback':0}
items=[('tone bass +63 on noise',c['st_tone_b+63_t+00'],{**NZ,'bass':63},[(0.05,60,1.2)],1.4),
 ('tone treble +63 on noise',c['st_tone_b+00_t+63'],{**NZ,'treble':63},[(0.05,60,1.2)],1.4),
 ('tone treble -64 on noise',c['st_tone_b+00_t-64'],{**NZ,'treble':-64},[(0.05,60,1.2)],1.4),
 ('delay mono long, time 64, click',c['st_dl_MONOLONG_t064'],{**CL,'delay.type':4,'delay.time':64},[(0.05,96,0.01)],2.9),
 ('delay panning L to R, time 64, click',c['st_dl_PANNINGLR_t064'],{**CL,'delay.type':0,'delay.time':64},[(0.05,96,0.01)],2.9),
 ('delay feedback 127 (time differs, see notes)',c['st_dlf_127'],{**CL,'delay.type':3,'delay.time':64,'delay.feedback':127},[(0.05,96,0.01)],3.0),
 ('chorus freeze phase 1 on noise',c['st_chn_10'],{'osc1.wave':2,'osc1.ctrl1':127,'chorus.type':10,'chorus':127},[(0.05,60,2.0)],2.2),
 ('chorus super slow on a triangle',c['st_ch_00'],{'osc1.wave':6,'osc1.ctrl1':0,'chorus.type':0,'chorus':127},[(0.05,72,3.0)],3.2)]
gap=np.zeros((2,int(0.5*SR)));out=[];key=[];t=0.0
for name,a,sets,notes,dur in items:
    b=ours(sets,notes,dur,stereo=True);n=min(a.shape[1],b.shape[1],int((dur+LAT+0.1)*SR));a=a[:,:n];b=b[:,:n];g=a.std()/max(b.std(),1e-9)
    for lab,x in (('A emulator',a),('B Trancera 0.6.0',b*g)):
        key.append(f'{t:6.1f} s  {lab:14s} {name}'+(f'   (trim on B {20*np.log10(g):+.1f} dB)' if lab[0]=='B' else ''));out+=[x,gap];t+=(x.shape[1]+gap.shape[1])/SR
    out.append(gap);t+=gap.shape[1]/SR
x=np.concatenate(out,1);x*=0.5/abs(x).max()
dd=sys.argv[1];os.makedirs(dd,exist_ok=True)
subprocess.run(['ffmpeg','-v','error','-y','-f','f32le','-ar',str(SR),'-ac','2','-i','-','-ar','48000','-c:a','pcm_s24le',dd+'/ab-round5-effects.wav'],input=x.T.astype('<f4').tobytes(),check=True)
open(dd+'/KEY.txt','w').write('A/B round 5: JE-8086 against Trancera 0.6.0, digital output node, stereo. Emulator first in each pair, level-matched.\n\n'+'\n'.join(key)+'\n')
print(open(dd+'/KEY.txt').read())
