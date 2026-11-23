import sys,os,subprocess; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('cmp4.py').read().split("rows=[]")[0])
X={**TRI,'osc2.wave':2,'osc2.range':7}
items=[('attack 32 on C6',d['sa_032'],{**TRI,'amp.a':32},[(0.05,84,0.5)],0.7,(),0),
 ('release 32 on C6',d['sr_032'],{**TRI,'amp.r':32},[(0.05,84,0.3)],1.0,(),0),
 ('LFO 1 fade 96, pitch',d['fa_096'],{**TRI,'pitchlfo':24,'lfo1.rate':96,'lfo1.fade':96},[(0.05,72,3.0)],3.2,(),0),
 ('LFO 1 random, pitch',d['lw_random'],{**TRI,'pitchlfo':24,'lfo1.rate':64,'lfo1.wave':3},[(0.05,72,2.5)],2.7,(),0),
 ('LFO 2 to pitch, lever at 64',d['l2m_064'],{**TRI,'lfo2.pitch':63,'lfo2.rate':80},[(0.05,72,2.0)],2.2,['0 mod 64'],0),
 ('amp LFO 63',d['st_al_+63'][0],{**TRI,'amplfo':63,'lfo1.rate':64},[(0.05,72,1.5)],1.7,(),0),
 ('auto pan 63 (stereo)',d['st_pan_auto_+63'],{**TRI,'pan':1,'amplfo':63,'lfo1.rate':64},[(0.05,72,1.5)],1.7,(),1),
 ('X-MOD 16',d['xm_016'],{**X,'xmod':16},[(0.05,60,0.9)],1.1,(),0),
 ('X-MOD 64',d['xm_064'],{**X,'xmod':64},[(0.05,60,0.9)],1.1,(),0),
 ('balance centre',d['bal_+00'],{**X,'balance':0},[(0.05,60,0.9)],1.1,(),0),
 ('noise cutoff 96',d['nz_c096'],{'osc1.wave':2,'osc1.ctrl1':96},[(0.05,60,1.3)],1.5,(),0),
 ('noise cutoff 64 resonance 112 on C5',d['nz_c064_r112_n72'],{'osc1.wave':2,'osc1.ctrl1':64,'osc1.ctrl2':112},[(0.05,72,1.3)],1.5,(),0)]
gap=np.zeros((2,int(0.5*SR)));out=[];key=[];t=0.0
for name,a,sets,notes,dur,extra,st in items:
    b=ours(sets,notes,dur,extra,stereo=bool(st));a=np.atleast_2d(a);b=np.atleast_2d(b)
    if a.shape[0]==1:a=np.vstack([a,a])
    if b.shape[0]==1:b=np.vstack([b,b])
    n=min(a.shape[1],b.shape[1],int((dur+LAT+0.1)*SR));a=a[:,:n];b=b[:,:n];g=a.std()/max(b.std(),1e-9)
    for lab,x in (('A emulator',a),('B Trancera 0.4.1',b*g)):
        key.append(f'{t:6.1f} s  {lab:14s} {name}');out+=[x,gap];t+=(x.shape[1]+gap.shape[1])/SR
    out.append(gap);t+=gap.shape[1]/SR
x=np.concatenate(out,1);x*=0.5/abs(x).max()
dd=sys.argv[1];os.makedirs(dd,exist_ok=True)
subprocess.run(['ffmpeg','-v','error','-y','-f','f32le','-ar',str(SR),'-ac','2','-i','-','-ar','48000','-c:a','pcm_s24le',dd+'/ab-round3-modulation.wav'],input=x.T.astype('<f4').tobytes(),check=True)
open(dd+'/KEY.txt','w').write('A/B round 3: JE-8086 (JP-8000 firmware 1.05) against Trancera 0.4.1, digital output node.\nEach pair is emulator first, then Trancera, level-matched per pair. Only the auto-pan pair is real stereo.\n\n'+'\n'.join(key)+'\n')
print(open(dd+'/KEY.txt').read(),x.shape[1]/SR)
