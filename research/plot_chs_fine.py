import csv,sys,numpy as np,matplotlib.pyplot as plt
source=sys.argv[1];output=sys.argv[2];label=sys.argv[3]
rows=list(csv.DictReader(open(source)))
n=max(int(r['pass']) for r in rows)
levels=np.full((81,n),np.nan);times=np.full(n,np.nan)
for r in rows:
 p=int(r['pass'])-1;k=int(r['step']);levels[k,p]=int(r['rssi_dbm_x10'])/10
 if np.isnan(times[p]):times[p]=int(r['elapsed_ms'])/1000
fig,ax=plt.subplots(figsize=(11,5.5))
im=ax.imshow(levels,origin='lower',aspect='auto',extent=(times[0],times[-1],862.9875,865.0125),vmin=-100,vmax=-15,cmap='viridis',interpolation='nearest')
ax.set_title(label);ax.set_xlabel('Seconds from scan start (RTC-compatible Flipper tick)');ax.set_ylabel('Receiver frequency (MHz)')
ax.set_yticks(np.arange(863,865.01,.25));ax.axvline(4,color='white',ls='--',lw=1,label='switch prompt');ax.legend(loc='lower left')
fig.colorbar(im,ax=ax,label='CC1101 OOK270 RSSI (dBm; clips near -14)')
fig.tight_layout();fig.savefig(output,dpi=180)
