"""Compare two empirical CHS alignments. No packet interpretation implied."""
import csv
import numpy as np

MARKER=f'{0x9B1BA5A6:032b}'
PERIOD=1984
CASES=[
 ('to_ch18','outputs/captures/chs_carrier_864475_decoded.csv',5,13),
 ('to_ch19','outputs/captures/chs_carrier_reverse_864475_decoded.csv',6,13),
]
profiles={}
for label,path,lo,hi in CASES:
    arr=[]
    for row in csv.DictReader(open(path)):
        t=int(row['elapsed_ms'])/1000
        if not lo<=t<hi or float(row['clock_coherence'])<.2:continue
        b=row['candidate_bits'];j=b.find(MARKER)
        if j<0:continue
        a=np.full(PERIOD,-1,dtype=np.int8)
        for k,v in enumerate(b):
            i=(k-j)%PERIOD
            if a[i]<0:a[i]=int(v)
        arr.append(a)
    x=np.stack(arr)
    profiles[label]=((x==1).sum(0),(x>=0).sum(0),len(arr))
    print(label,len(arr))
with open('outputs/captures/chs_direction_bit_comparison.csv','w',newline='') as f:
    w=csv.writer(f)
    w.writerow(['position_after_marker','ch18_ones','ch18_observed','ch18_p1','ch19_ones','ch19_observed','ch19_p1','opposite_fixed_95pct'])
    a,an,_=profiles['to_ch18'];b,bn,_=profiles['to_ch19']
    flips=[]
    for i in range(PERIOD):
        pa=a[i]/an[i];pb=b[i]/bn[i]
        flip=(pa<.05 and pb>.95) or (pa>.95 and pb<.05)
        if flip:flips.append(i)
        w.writerow([i,a[i],an[i],round(pa,6),b[i],bn[i],round(pb,6),int(flip)])
print('opposite fixed positions',flips)
