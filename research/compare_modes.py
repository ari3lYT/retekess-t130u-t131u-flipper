import csv,numpy as np,collections
P=1984
modes=[
 ('normal19','outputs/captures/ch19_t131u_on_baseline_decoded.csv',0x9B1BA525,lambda t: True),
 ('alloff','outputs/captures/ch19_alloff_event_decoded.csv',0x9B1BA525,lambda t: 8<=t<16),
 ('chs19','outputs/captures/chs_carrier_reverse_864475_decoded.csv',0x9B1BA5A6,lambda t: 6<=t<13),
 ('chs18','outputs/captures/chs_carrier_864475_decoded.csv',0x9B1BA5A6,lambda t: 6<=t<12),
 ('chs16','outputs/captures/chs_carrier_to16_864475_decoded.csv',0x9B1BA5A6,lambda t: 6<=t<13),
]
profiles={}
for label,path,marker,predicate in modes:
 arr=[]
 for q in csv.DictReader(open(path)):
  t=int(q.get('elapsed_ms') or 0)/1000
  if not predicate(t):continue
  b=q['candidate_bits'];j=b.find(f'{marker:032b}')
  if j<0:continue
  a=np.full(P,-1,dtype=np.int8)
  for k,v in enumerate(b):
   idx=(k-j)%P
   if a[idx]<0:a[idx]=int(v)
  arr.append(a)
 x=np.stack(arr);p1=np.divide((x==1).sum(0),(x>=0).sum(0))
 profiles[label]=p1
 print(label,len(x),'stable',sum((p1<.01)|(p1>.99)))
for a,b in [('normal19','alloff'),('normal19','chs19'),('chs19','chs18'),('chs19','chs16')]:
 p=profiles[a];q=profiles[b]
 flips=np.flatnonzero(((p<.05)&(q>.95))|((p>.95)&(q<.05)))
 print(a,b,'fixed flips',len(flips),'positions',flips.tolist()[:180])
 print('ranges',[(k,len(list(v))) for k,v in []])
