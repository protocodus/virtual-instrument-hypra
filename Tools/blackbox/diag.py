import sys,os,json; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ['PLAIN']='p'
sys.argv=['x','/private/tmp/claude-501/-Users-vojta-Dev/da1214f3-867b-4c3a-9878-bff86a0278c9/scratchpad/je/pab',os.environ.get('TMPDIR','/tmp')+'/abp']
src=open('presets_cmp.py').read().split('rows=[]')[0];exec(src)
def show(k,mods=()):
    v=d[k];a=np.load(f'{P}/jep_{int(k):03d}.npy').astype(float)
    params=dict(v['params'])
    ev=['0 node digital']+m.events(params)+list(mods);tl=[]
    for t,n,vv,l in v['notes']:tl+=[(t,f'{t} note {n} {vv}'),(t+l,f'{t+l} off {n}')]
    ev+=[e for _,e in sorted(tl,key=lambda q:q[0])]
    open(f'{P}/ev.txt','w').write('\n'.join(ev)+'\n');subprocess.run([H,f'{P}/ev.txt',f'{P}/o.f32',str(SR),'6.5','stereo'],check=True)
    b=np.fromfile(f'{P}/o.f32','<f4').astype(float).reshape(-1,2).T;b=np.concatenate([np.zeros((2,int(LAT*SR))),b],1)
    n=min(a.shape[1],b.shape[1]);ba=bands(a[:,:n]);bb=bands(b[:,:n])
    print(k,m.name(v['params']),mods,'emu',' '.join(f'{x:4.0f}' for x in ba[::2]));print('   ours',' '.join(f'{x:4.0f}' for x in bb[::2]))
for k,mods in (('10',()),('10',('0 set oscshift 0',)),('10',('0 set keyfollow 0',)),('10',('0 set chorus 0','0 set delay 0'))):show(k,mods)
print('bands',' '.join(f'{int(b)}' for b in B[::2]))
