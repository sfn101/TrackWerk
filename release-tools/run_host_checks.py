#!/usr/bin/env python3
"""Run every built TrackWerk host check, with real preset manifests and isolated outputs."""
import argparse,json,re,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();source=a.source.resolve();build=a.build.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
rows=[]
for f in sorted((source/'factory/Presets/Instruments').rglob('*.pti')):
 j=json.loads(f.read_text());i=j.get('instrument',j);rows.append(dict(path=str(f),name=i.get('name',''),category=f.parent.name))
manifest=out/'preset-manifest.json';manifest.write_text(json.dumps(dict(presets=rows)));results=[]
for name in re.findall(r'add_executable\((pt[a-z0-9-]+)',(source/'native/CMakeLists.txt').read_text()):
 if name=='ptsynth-cpu-bench':continue
 exe=build/name;folder=out/name
 if not exe.is_file():raise SystemExit('Missing host check: '+name)
 folder.mkdir(exist_ok=True);args=[str(folder/'out')]
 if name=='ptpresetpack-check':args=[str(manifest),str(folder/'out')]
 elif name=='pteffect-collection-check':args=[str(source/'factory/Presets'),str(folder/'out')]
 elif name=='ptcorrupt-files-check':args=[str(source/'tests/corrupt-files')]
 elif name in ('ptcapture-input-policy-check','ptfft-roundtrip-check'):args=[]
 start=time.monotonic()
 with (out/(name+'.log')).open('w') as log:r=subprocess.run([str(exe),*args],cwd=folder,stdout=log,stderr=subprocess.STDOUT)
 result=dict(name=name,return_code=r.returncode,seconds=round(time.monotonic()-start,3));results.append(result);print(name,r.returncode,flush=True)
(out/'HOST-CHECKS.json').write_text(json.dumps(results,indent=2));failed=[r for r in results if r['return_code']!=0];print(str(len(results)-len(failed))+'/'+str(len(results))+' host checks passed');raise SystemExit(bool(failed))
