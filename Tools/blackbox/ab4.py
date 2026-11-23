import sys,os,subprocess; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('cmp3.py').read().split("rows=[]")[0])
FE={**FC,'cutoff':40,'envdepth':40}
items=[('amp attack 64',c4['aa_064'],{**TRI,'amp.a':64},[(0.05,60,1.3)],1.5),
 ('amp release 64',c4['ar_064'],{**TRI,'amp.r':64},[(0.05,60,0.3)],1.4),
 ('amp decay 80, sustain 0 (second note)',c3c['ad_080'],{**TRI,'amp.s':0,'amp.d':80},[(0.05,60,0.3),(0.5,60,2.5)],3.2),
 ('pitch LFO depth 32, rate 80',c4['ld_d+32'],{**TRI,'pitchlfo':32,'lfo1.rate':80},[(0.05,72,2.5)],2.7),
 ('LFO rate 112',c4['lr_112'],{**TRI,'pitchlfo':24,'lfo1.rate':112},[(0.05,72,1.5)],1.7),
 ('pitch env +44, decay 64',c4['pe_d+44'],{**TRI,'pitchenv':44,'pitchenv.d':64},[(0.05,72,1.6)],1.8),
 ('filter env 40 on noise, decay 80, sustain 0',c3c['fd_080'],{**FE,'fenv.s':0,'fenv.d':80},[(0.05,60,0.3),(0.5,60,2.5)],3.2),
 ('filter LFO 24 on noise',c4['lf_d+24'],{**FC,'cutoff':64,'filterlfo':24,'lfo1.rate':48},[(0.05,60,2.8)],3.0),
 ('key follow +40, note 84',c4['kf_k+40_n84'],{**FC,'cutoff':64,'keyfollow':40},[(0.05,84,0.9)],1.1),
 ('portamento 64, 48 to 72',c4['po24_064'],{'osc1.wave':6,'mono':1,'portamento':1,'portamento.time':64},[(0.05,48,0.7),(0.6,72,1.2)],2.0)]
gap=np.zeros(int(0.5*SR));out=[];key=[];t=0.0
for name,a,sets,notes,dur in items:
    b=ours(sets,notes,dur);n=min(len(a),len(b),int((dur+LAT+0.1)*SR));a=a[:n];b=b[:n]
    g=a.std()/max(b.std(),1e-9)
    for lab,x in (('A emulator',a),('B Hypra 0.4.1',b*g)):
        key.append(f'{t:6.1f} s  {lab:14s} {name}');out+=[x,gap];t+=(len(x)+len(gap))/SR
    out.append(gap);t+=len(gap)/SR
x=np.concatenate(out);x*=0.5/abs(x).max()
d=sys.argv[1];os.makedirs(d,exist_ok=True)
subprocess.run(['ffmpeg','-v','error','-y','-f','f32le','-ar',str(SR),'-ac','1','-i','-','-ar','48000','-c:a','pcm_s24le',d+'/ab-round2-timing.wav'],input=x.astype('<f4').tobytes(),check=True)
open(d+'/KEY.txt','w').write('A/B round 2: JE-8086 (JP-8000 firmware 1.05) against Hypra 0.4.1, digital output node.\nEach pair is emulator first, then Hypra, level-matched per pair. Times are from the start of the file.\n\n'+'\n'.join(key)+'\n')
print(open(d+'/KEY.txt').read(),len(x)/SR)
