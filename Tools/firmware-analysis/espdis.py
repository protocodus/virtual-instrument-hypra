"""ESP word disassembler for the analysis dumps (field meanings from gearmulator's esp_common.h)."""
import numpy as np,sys
SH=lambda b:(0x3567>>((b&3)<<2))&15
OPS={0x00:'mac',0x04:'mac=',0x10:'macB',0x14:'macB=',0x08:'st=',0x0c:'stB=',0x18:'st=B',0x1c:'stB=B',0x58:'st+',0x5c:'stB+B',
     0x20:'gread',0x24:'gread=',0x28:'gst=',0x2c:'gstB=',0x30:'mul30',0x34:'spec',0x38:'gst+',0x3c:'gstB+',0x40:'straw',0x44:'straw=',
     0x48:'strect',0x4c:'strect=',0x50:'cmp',0x54:'op54',0x60:'inv',0x70:'inv',0x64:'inv=',0x74:'inv=',0x68:'fold+',0x6c:'fold+=',0x78:'fold-',0x7c:'fold-='}
def dis(w,core):
    if w==0:return 'nop'
    field=(w>>23)&31;v=w&0x7fffff;op=(v>>16)&0x7c;mem=(v>>10)&0xff;sb=(v>>8)&3;coef=np.int8(v&0xff)
    name=OPS.get(op,f'op{op:02x}')
    c16=int(coef)*(1<<(7-SH(sb)))
    src=f'#{0x10<<(6*(mem-1)):x}' if 1<=mem<=4 else f'i[{mem:02x}]'
    s=f'{name:7s} {src:9s} k={int(coef):4d} sh={SH(sb)} (x{c16/128:+.5f})'
    if op==0x34:s=f'spec    m={mem:02x} sub={mem&15:x} k={int(coef):4d}'
    if op in(0x20,0x24,0x28,0x2c,0x38,0x3c):s=f'{name:7s} g[{mem:02x}] k={int(coef):4d}'
    if core==1 and field:s+=f'  eram:{field:02x}'
    return s
if __name__=='__main__':
    d=np.fromfile(sys.argv[1],'<u4').reshape(-1,2,1024);chip=int(sys.argv[2]);core=int(sys.argv[3])
    for pc in range(int(sys.argv[4]) if len(sys.argv)>4 else 0,int(sys.argv[5]) if len(sys.argv)>5 else 768):
        w=int(d[chip,core,pc])
        if w:print(f'{pc:3d} {w:08x}  {dis(w,core)}')
