import csv
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path

source=Path('outputs/captures/chs_19to18_spectrum.csv')
rows=list(csv.DictReader(source.open()))
passes=max(int(r['pass']) for r in rows)
levels=np.full((19,passes),np.nan)
times=np.full(passes,np.nan)
for r in rows:
    p=int(r['pass'])-1;ch=int(r['channel'])-1
    levels[ch,p]=int(r['rssi_dbm_x10'])/10
    if np.isnan(times[p]):times[p]=int(r['elapsed_ms'])/1000
fig,ax=plt.subplots(figsize=(11,5))
im=ax.imshow(levels,aspect='auto',origin='lower',extent=(times[0],times[-1],0.5,19.5),vmin=-100,vmax=-20,cmap='viridis',interpolation='nearest')
ax.set_yticks(range(1,20,2));ax.set_xlabel('Seconds from scan start (Flipper tick; no IRQ masking)');ax.set_ylabel('Receiver channel center')
ax.set_title('T130U CH19→CH18: RSSI survey of 19 channel centers')
ax.axvline(4,color='white',ls='--',alpha=.8,label='screen prompt begins')
ax.legend(loc='lower left',framealpha=.8)
fig.colorbar(im,ax=ax,label='CC1101 OOK270 RSSI (dBm)')
fig.tight_layout();fig.savefig('outputs/chs_19to18_spectrum.png',dpi=180)
