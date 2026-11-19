import sys,glob,os,wave,numpy as np
def load(f):
    w=wave.open(f); n=w.getnframes(); sr=w.getframerate(); ch=w.getnchannels(); sw=w.getsampwidth()
    raw=w.readframes(n)
    if sw==2: a=np.frombuffer(raw,dtype=np.int16).astype(float)/32768
    elif sw==3:
        b=np.frombuffer(raw,dtype=np.uint8).reshape(-1,3); a=(b[:,0].astype(np.int32)|(b[:,1].astype(np.int32)<<8)|(b[:,2].astype(np.int32)<<16)); a=np.where(a>=1<<23,a-(1<<24),a)/float(1<<23)
    else: a=np.frombuffer(raw,dtype=np.float32).astype(float)
    a=a.reshape(-1,ch).mean(1); return a,sr
def analyse(f):
    a,sr=load(f)
    N=8192; hop=4096; win=np.hanning(N)
    chroma=np.zeros(12); cents=[]; wts=[]
    for i in range(0,len(a)-N,hop):
        fr=a[i:i+N]*win
        if np.sqrt((fr**2).mean())<1e-3: continue
        X=np.abs(np.fft.rfft(fr)); f0=np.arange(len(X))*sr/N
        # peaks between 60 Hz and 2.5 kHz
        lo=int(float(os.environ.get("LOHZ","60"))*N/sr); hi=int(float(os.environ.get("HIHZ","2500"))*N/sr)
        seg=X[lo:hi]; thr=seg.max()*0.12
        pk=np.where((seg[1:-1]>seg[:-2])&(seg[1:-1]>seg[2:])&(seg[1:-1]>thr))[0]+1
        # a note stands well above the noise around it (shakers and hats don't)
        pk=[p for p in pk if seg[p] > 8*np.median(seg[max(0,p-40):p+40])]
        fs=[]
        for p in pk:
            k=p+lo; y0,y1,y2=np.log(X[k-1]+1e-12),np.log(X[k]+1e-12),np.log(X[k+1]+1e-12)
            d=0.5*(y0-y2)/(y0-2*y1+y2+1e-12); fs.append(((k+d)*sr/N, X[k]**2))
        for fr_,w_ in fs:
            # a harmonic of a lower note (2nd to 9th) is that note's colour, not a note of its own
            if any(abs(fr_/q - round(fr_/q))<0.02*round(fr_/q) and 2<=round(fr_/q)<=9 for q,_ in fs if q<fr_*0.95): continue
            midi=69+12*np.log2(fr_/440.0); m=int(round(midi))
            chroma[m%12]+=w_; cents.append((midi-m)*100); wts.append(w_)
    if chroma.sum()==0: return None
    cents=np.array(cents); wts=np.array(wts)
    order=np.argsort(cents); cw=np.cumsum(wts[order]); med=cents[order][np.searchsorted(cw,cw[-1]/2)]
    return chroma/chroma.sum(), med
names="C Db D Eb E F Gb G Ab A Bb B".split()
def scale(key,major,steps=None):
    st=steps or ([0,2,4,5,7,9,11] if major else [0,2,3,5,7,8,10])
    return {(key+s)%12 for s in st}
if __name__=="__main__":
    root=sys.argv[1]; key=int(sys.argv[2]); major=sys.argv[3]=='major'
    steps=[int(x) for x in sys.argv[4].split(',')] if len(sys.argv)>4 else None
    sc=scale(key,major,steps)
    for f in sorted(glob.glob(root+'/*.wav')):
        r=analyse(f)
        if r is None: print(os.path.basename(f),'silencio'); continue
        c,med=r
        out=sum(c[i] for i in range(12) if i not in sc)
        top=' '.join(f"{names[i]}{int(c[i]*100)}" for i in np.argsort(-c)[:5])
        print(f"{os.path.basename(f)[:26]:26s} fuera {out*100:5.1f}%  afinación {med:+5.1f}c  | {top}")
