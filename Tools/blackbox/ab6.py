import sys,os,subprocess; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('cmp4.py').read().split("rows=[]")[0])
c7=np.load('cap7.npz');c8=np.load('cap8.npz');c10=np.load('cap10.npz');c9=np.load('cap9.npz')
S={'osc1.wave':0}
X={'osc1.wave':6,'osc1.ctrl1':0,'osc2.wave':2,'osc2.range':7,'ring':1}
items=[('supersaw detune 124, mix 0',c8['sd_124'],{**S,'osc1.ctrl1':124,'osc1.ctrl2':0},[(0.05,72,2.2)],2.4),
 ('supersaw detune 100, mix 0',c8['sd_100'],{**S,'osc1.ctrl1':100,'osc1.ctrl2':0},[(0.05,72,2.2)],2.4),
 ('supersaw detune 64, mix 127',c7['sm_127'],{**S,'osc1.ctrl1':64,'osc1.ctrl2':127},[(0.05,72,3.6)],3.8),
 ('ring, triangles a fifth apart',c7['ring_b+63'],{**X,'balance':63},[(0.05,60,0.9)],1.1),
 ('ring, saws a fifth apart',c7['ring_saw_b+63'],{**X,'osc1.wave':5,'osc2.wave':1,'balance':63},[(0.05,60,0.9)],1.1),
 ('feedback harmonics 64, amount 64',c10['fb_c064_a064'],{'osc1.wave':3,'osc1.ctrl1':64,'osc1.ctrl2':64},[(0.05,48,1.4)],1.6),
 ('feedback harmonics 32, amount 64',c10['fb_c032_a064'],{'osc1.wave':3,'osc1.ctrl1':32,'osc1.ctrl2':64},[(0.05,48,1.4)],1.6),
 ('feedback harmonics 16, amount 127',c10['fb_c016_a127'],{'osc1.wave':3,'osc1.ctrl1':16,'osc1.ctrl2':127},[(0.05,48,1.4)],1.6),
 ('feedback harmonics 64, amount 32',c9['fb_c064_a032'],{'osc1.wave':3,'osc1.ctrl1':64,'osc1.ctrl2':32},[(0.05,48,1.4)],1.6)]
gap=np.zeros(int(0.5*SR));out=[];key=[];t=0.0
for name,a,sets,notes,dur in items:
    b=ours(sets,notes,dur);n=min(len(a),len(b),int((dur+LAT+0.1)*SR));a=a[:n];b=b[:n];g=a.std()/max(b.std(),1e-9)
    for lab,x in (('A emulator',a),('B Hypra 0.5.0',b*g)):
        key.append(f'{t:6.1f} s  {lab:14s} {name}   (level trim on B: {20*np.log10(g):+.1f} dB)' if lab[0]=='B' else f'{t:6.1f} s  {lab:14s} {name}');out+=[x,gap];t+=(len(x)+len(gap))/SR
    out.append(gap);t+=len(gap)/SR
x=np.concatenate(out);x*=0.5/abs(x).max()
dd=sys.argv[1];os.makedirs(dd,exist_ok=True)
subprocess.run(['ffmpeg','-v','error','-y','-f','f32le','-ar',str(SR),'-ac','1','-i','-','-ar','48000','-c:a','pcm_s24le',dd+'/ab-round4-oscillators.wav'],input=x.astype('<f4').tobytes(),check=True)
open(dd+'/KEY.txt','w').write('A/B round 4: JE-8086 (JP-8000 firmware 1.05) against Hypra 0.5.0, digital output node.\nEach pair is emulator first, then Hypra, level-matched per pair; the trim shows how far the raw levels differ.\n\n'+'\n'.join(key)+'\n')
print(open(dd+'/KEY.txt').read())
