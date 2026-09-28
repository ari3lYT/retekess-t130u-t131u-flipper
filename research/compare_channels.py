"""Compare empirical bit positions for ordinary CH18 and CH19 streams.

Alignment sequences and 1984-bit period are measured, not a proven frame format.
"""
import csv
from pathlib import Path
import numpy as np

PERIOD=1984
CASES=[
    ('ch18','outputs/captures/ch18_old_chs_event_decoded.csv',0x9B1BA4A5,4.5),
    ('ch19','outputs/captures/ch19_t131u_on_baseline_decoded.csv',0x9B1BA525,None),
]
profiles={}
for label,path,marker,limit in CASES:
    aligned=[]
    with open(path,newline='') as f:
        for row in csv.DictReader(f):
            if limit is not None and float(row['elapsed_ms'])/1000>=limit:continue
            bits=row['candidate_bits'];at=bits.find(f'{marker:032b}')
            if at<0:continue
            frame=np.full(PERIOD,-1,dtype=np.int8)
            for index,bit in enumerate(bits):
                position=(index-at)%PERIOD
                if frame[position]<0:frame[position]=int(bit)
            aligned.append(frame)
    arr=np.stack(aligned)
    profiles[label]=((arr==1).sum(axis=0),(arr>=0).sum(axis=0),len(arr))
    print(label,len(arr))

with open('outputs/captures/ch18_ch19_stable_comparison.csv','w',newline='') as f:
    writer=csv.writer(f)
    writer.writerow(['position_after_marker','ch18_ones','ch18_observed','ch18_p1','ch19_ones','ch19_observed','ch19_p1','opposite_fixed_95pct'])
    (ones18,obs18,_)=profiles['ch18'];(ones19,obs19,_)=profiles['ch19']
    for position in range(PERIOD):
        p18=ones18[position]/obs18[position]
        p19=ones19[position]/obs19[position]
        opposite=(p18<.05 and p19>.95) or (p18>.95 and p19<.05)
        writer.writerow([position,ones18[position],obs18[position],round(p18,6),ones19[position],obs19[position],round(p19,6),int(opposite)])
