from pathlib import Path
import subprocess,shutil,os,json,hashlib,zipfile,tarfile,io,gzip,stat,struct
R=Path(os.environ.get('TRACKWERK_RELEASE_WORKSPACE',str(Path(__file__).resolve().parents[3])));SRC=R/'source/source';OUT=R/'packages';D=R/'desktop-stage';D.mkdir(exist_ok=True);assert R.resolve()==R
CLAUDE=Path(os.environ.get('TRACKWERK_RELEASE_INPUTS',str(R/'release-inputs')));PACK=CLAUDE/'sample-pack/ausgabe';EPOCH=1795176000
samples={}
for folder in ['TrackWerk-Sample-Pack-1.3','TrackWerk-Drum-Machines-Samples']:
 for p in sorted((PACK/folder).rglob('*')):
  if p.is_file() and not any(x.startswith('.') for x in p.relative_to(PACK).parts):samples[p.relative_to(PACK).as_posix()]=p.read_bytes()
assert sum(k.endswith('.wav') for k in samples)==1528
manuals={n:(R/'manual-review'/n).read_bytes() for n in ['TrackWerk-1.3-Manual-EN.pdf','TrackWerk-1.3-Handbuch-DE.pdf']}
def sha(b):return hashlib.sha256(b).hexdigest()
def copytree(src,dest):shutil.copytree(src,dest,dirs_exist_ok=True,ignore=shutil.ignore_patterns('.*','._*'))
def put(root,rel,b,mode=0o644):
 p=root/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b.encode() if isinstance(b,str) else b);p.chmod(mode)
def add_common(root,sample_path,manual_path):
 for n,b in samples.items():put(root,sample_path+'/'+n,b)
 for n,b in manuals.items():put(root,manual_path+'/'+n,b)
def packzip(stage,path):
 with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  for p in sorted(stage.rglob('*')):
   if p.is_file() and not p.is_symlink():
    i=zipfile.ZipInfo(p.relative_to(stage).as_posix(),(2026,11,20,12,0,0));i.create_system=3;i.compress_type=zipfile.ZIP_DEFLATED;i.external_attr=(stat.S_IFREG|stat.S_IMODE(p.stat().st_mode))<<16;z.writestr(i,p.read_bytes())
results=[]
# Portable Windows package retains MSVC/static CRT/SDL evidence and adds the requested media.
w=D/'windows';w.mkdir(exist_ok=True)
with zipfile.ZipFile(R/'ci-artifacts/windows/source/build/windows/TrackWerk-1.3.0-windows-x64.zip') as z:z.extractall(w)
add_common(w,'TrackWerk/Samples','TrackWerk/Handbuch-Manual')
copytree(SRC/'factory/Presets',w/'TrackWerk/Presets')
copytree(SRC/'app/src/main/assets',w/'TrackWerk')
put(w,'TrackWerk/Start-TrackWerk.bat',r'''@echo off
setlocal
if not defined TRACKWERK_HOME if defined POCKETTRACKER_HOME set "TRACKWERK_HOME=%POCKETTRACKER_HOME%"
if not defined TRACKWERK_HOME if exist "%USERPROFILE%\Documents\TrackWerk" set "TRACKWERK_HOME=%USERPROFILE%\Documents\TrackWerk"
if not defined TRACKWERK_HOME set "TRACKWERK_HOME=%LOCALAPPDATA%\TrackWerk"
if not exist "%TRACKWERK_HOME%" mkdir "%TRACKWERK_HOME%"
robocopy "%~dp0Samples" "%TRACKWERK_HOME%\Samples" /E /XC /XN /XO /NFL /NDL /NJH /NJS >nul
if errorlevel 8 exit /b 1
robocopy "%~dp0Presets" "%TRACKWERK_HOME%\Presets" /E /XC /XN /XO /NFL /NDL /NJH /NJS >nul
if errorlevel 8 exit /b 1
start "" "%~dp0TrackWerk.exe" %*
'''.replace('\n','\r\n'))
wincommit=(R/'ci-artifacts/windows/evidence/SOURCE-COMMIT.txt').read_text(encoding='utf-8-sig').strip();put(w,'TrackWerk/BUILD-IDENTITY.json',json.dumps({'source_commit':wincommit,'compiler':'MSVC 2022','native_window_tested_in_CI':True,'audible_hardware_tested':False,'test_scope':(R/'ci-artifacts/windows/evidence/TEST-SCOPE.txt').read_text(encoding='utf-8-sig')},indent=2))
put(w,'START-HERE.txt','TrackWerk 1.3.0 for Windows x64\nExtract the complete ZIP and run TrackWerk/Start-TrackWerk.bat. Samples and factory presets are added only when missing. Existing user files are preserved. A new data folder uses LOCALAPPDATA/TrackWerk; an existing Documents/TrackWerk folder remains active. English and German manuals are in TrackWerk/Handbuch-Manual. MSVC build, static CRT/SDL checks and native window startup passed in Windows CI. Audible sound-device testing is still pending.\n')
p=OUT/'TrackWerk-1.3.0-Windows-x64.zip';packzip(w,p);results.append({'file':p.name,'sha256':sha(p.read_bytes()),'source_commit':wincommit,'status':'MSVC build and native window startup passed; audible device test pending'})
# Native Debian 13 binaries; install scripts never remove user data.
for arch in ['amd64','arm64']:
 c=R/'ci-artifacts'/('linux-'+arch);binary=c/'trackwerk.bin';assert binary.is_file();commit=(c/'SOURCE-COMMIT.txt').read_text().strip();stage=D/('linux-'+arch);stage.mkdir(exist_ok=True)
 put(stage,'usr/lib/trackwerk/trackwerk.bin',binary.read_bytes(),0o755)
 launcher=(R/'source/release-tools/1.2/linux/packaging/trackwerk').read_text().replace('PACKAGE_VERSION="1.2.0-1"','PACKAGE_VERSION="1.3.0-1"').replace('SOURCE_COMMIT="b24ad7abe43f6784cb8ef34dee7661db0d02dade"','SOURCE_COMMIT="'+commit+'"').replace('def seed_presets(factory,root):','def seed_presets(factory,root,folder="Presets"):').replace('dest=root/"Presets"','dest=root/folder').replace("; upstream runtime label 1.1.0",'; native runtime 1.3.0')
 launcher=launcher.replace("    env=os.environ.copy();env['TRACKWERK_HOME']=str(root)","    try:seed_presets(prefix/'share/trackwerk/samples',root,'Samples')\n    except OSError as e:print('TrackWerk: sample setup incomplete: '+str(e),file=sys.stderr)\n    env=os.environ.copy();env['TRACKWERK_HOME']=str(root)")
 put(stage,'usr/bin/trackwerk',launcher,0o755)
 copytree(SRC/'app/src/main/assets',stage/'usr/lib/trackwerk');copytree(SRC/'factory',stage/'usr/share/trackwerk/factory')
 copytree(R/'source/release-tools/1.2/linux/packaging',stage/'usr/share/doc/trackwerk/packaging-reference')
 put(stage,'usr/share/applications/org.kidbunka.TrackWerk.desktop',(R/'source/release-tools/1.2/linux/packaging/org.kidbunka.TrackWerk.desktop').read_bytes())
 put(stage,'usr/share/icons/hicolor/512x512/apps/org.kidbunka.TrackWerk.png',(SRC/'branding/trackwerk-icon.png').read_bytes())
 for n in ['LICENSE','CREDITS.md','STUDIO-NOTES.txt']:
  if (SRC/n).exists():put(stage,'usr/share/doc/trackwerk/'+n,(SRC/n).read_bytes())
 copytree(SRC/'docs/licenses',stage/'usr/share/doc/trackwerk/licenses')
 add_common(stage,'usr/share/trackwerk/samples','usr/share/doc/trackwerk')
 put(stage,'usr/share/doc/trackwerk/BUILD-IDENTITY.json',json.dumps({'source_commit':commit,'native_binary_sha256':sha(binary.read_bytes()),'compiler':'GCC in Debian 13','native_window_Xvfb_test':True,'audio_test':'dummy device; audible hardware pending'},indent=2))
 put(stage,'usr/share/doc/trackwerk/README-LINUX.txt','TrackWerk 1.3.0 for Debian 13. Install with sudo apt install ./trackwerk_1.3.0-1_'+arch+'.deb and run trackwerk. Samples and presets are added only when missing. User data is not owned or removed by this package. TRACKWERK_HOME overrides the data folder. Manuals EN/DE are in /usr/share/doc/trackwerk. Native engine checks, Xvfb startup and dynamic dependencies passed in Debian 13 CI; audible hardware testing is pending.\n')
 files=sorted(p for p in stage.rglob('*') if p.is_file() and 'DEBIAN' not in p.relative_to(stage).parts)
 control='Package: trackwerk\nVersion: 1.3.0-1\nArchitecture: '+arch+'\nMaintainer: Kid Bunka <noreply@localhost>\nInstalled-Size: '+str(sum((x.stat().st_size+1023)//1024 for x in files))+'\nSection: sound\nPriority: optional\nDepends: libc6 (>= 2.41), libstdc++6 (>= 14), libgcc-s1, libsdl2-2.0-0 (>= 2.32.4), python3 (>= 3.9), libcurl4t64 | libcurl4, ca-certificates, ffmpeg\nHomepage: https://github.com/kidbee85-beep/TrackWerk\nDescription: TrackWerk music tracker and synthesis studio\n Eight tracks, sampler, 128 synth models and the Kid Bunka Sample Pack.\n Includes English and German manuals. Existing user data is preserved.\n'
 def tar(items):
  b=io.BytesIO()
  with tarfile.open(fileobj=b,mode='w',format=tarfile.GNU_FORMAT) as t:
   parents=set()
   for rel,_,_ in items:
    parents.update(str(x) for x in Path(rel).parents if str(x)!='.')
   for rel in sorted(parents,key=lambda x:(len(Path(x).parts),x)):
    i=tarfile.TarInfo('./'+rel+'/');i.type=tarfile.DIRTYPE;i.mode=0o755;i.uid=i.gid=0;i.uname=i.gname='root';i.mtime=EPOCH;t.addfile(i)
   for rel,data,mode in items:
    i=tarfile.TarInfo(rel);i.uid=i.gid=0;i.uname=i.gname='root';i.mtime=EPOCH;i.mode=mode;i.size=len(data);t.addfile(i,io.BytesIO(data))
  return gzip.compress(b.getvalue(),compresslevel=6,mtime=EPOCH)
 ctl=tar([('./control',control.encode(),0o644),('./md5sums',''.join(hashlib.md5(p.read_bytes()).hexdigest()+'  '+p.relative_to(stage).as_posix()+'\n' for p in files).encode(),0o644)])
 payload=tar([('./'+p.relative_to(stage).as_posix(),p.read_bytes(),stat.S_IMODE(p.stat().st_mode)) for p in files]);data=b'!<arch>\n'
 for name,b in [('debian-binary',b'2.0\n'),('control.tar.gz',ctl),('data.tar.gz',payload)]:data+=(name.ljust(16)+str(EPOCH).ljust(12)+'0'.ljust(6)+'0'.ljust(6)+'100644'.ljust(8)+str(len(b)).ljust(10)+'`\n').encode()+b+(b'\n' if len(b)%2 else b'')
 p=OUT/('trackwerk_1.3.0-1_'+arch+'.deb');p.write_bytes(data);rep={'file':p.name,'path':str(p),'sha256':sha(data),'source_commit':commit,'samples':1528,'manuals':2,'maintainer_scripts':[],'status':'native CI passed; package install acceptance pending'};(p.with_suffix('.deb.json')).write_text(json.dumps(rep,indent=2));results.append(rep);print('DEB_READY',arch,len(data),flush=True)
# Preserve the original Mac app's assets/notices, then replace its executable with the locally rebuilt one.
mac=D/'macos';mac.mkdir(exist_ok=True)
with zipfile.ZipFile(CLAUDE/'release-1.3.0-pakete-2026-11-20/TrackWerk-1.3.0-macOS-arm64-TEST.zip') as z:z.extractall(mac)
app=mac/'TrackWerk.app';contents=app/'Contents';res=contents/'Resources';bin=contents/'MacOS/trackwerk.bin';put(mac,str(bin.relative_to(mac)),(R/'native-host/pockettracker-sdl').read_bytes(),0o755)
add_common(res,'Samples','Handbuch-Manual');copytree(SRC/'factory/Presets',res/'Presets');copytree(SRC/'app/src/main/assets',res)
launcher='''#!/bin/sh
set -e
CONTENTS=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RES="$CONTENTS/Resources"
if [ -z "${TRACKWERK_HOME:-}" ]; then
 if [ -n "${POCKETTRACKER_HOME:-}" ]; then TRACKWERK_HOME="$POCKETTRACKER_HOME"; elif [ -d "$HOME/Documents/TrackWerk" ]; then TRACKWERK_HOME="$HOME/Documents/TrackWerk"; else TRACKWERK_HOME="$HOME/Library/Application Support/TrackWerk"; fi
fi
export TRACKWERK_HOME
mkdir -p "$TRACKWERK_HOME/Samples" "$TRACKWERK_HOME/Presets"
/bin/cp -nR "$RES/Samples/." "$TRACKWERK_HOME/Samples/"
/bin/cp -nR "$RES/Presets/." "$TRACKWERK_HOME/Presets/"
cd "$RES"
exec "$CONTENTS/MacOS/trackwerk.bin" "$@"
'''
put(contents,'MacOS/TrackWerk',launcher,0o755)
if (contents/'_CodeSignature').exists():shutil.rmtree(contents/'_CodeSignature')
subprocess.run(['/usr/bin/codesign','--force','--deep','--sign','-',str(app)],check=True,capture_output=True)
subprocess.run(['/usr/bin/codesign','--verify','--deep','--strict',str(app)],check=True,capture_output=True)
put(mac,'START-HERE.txt','TrackWerk 1.3.0 Apple Silicon test package. macOS 12+. Open TrackWerk.app. Ad-hoc signed, not notarised; no Apple Developer identity is claimed. Samples and presets are added only when missing; existing data is retained. EN/DE manuals are in the app Resources/Handbuch-Manual and beside the app. Audible hardware acceptance is pending.\n')
for n,b in manuals.items():put(mac,'Handbuch-Manual/'+n,b)
p=OUT/'TrackWerk-1.3.0-macOS-arm64-TEST.zip';packzip(mac,p);results.append({'file':p.name,'sha256':sha(p.read_bytes()),'source_commit':'028619182cf79d5f59c7718da8b71756d8512e3b','signature':'ad-hoc verified; not notarised','status':'window/audio-device acceptance pending'})
(R/'reports/DESKTOP-PACKAGES.json').write_text(json.dumps(results,indent=2)+'\n');print('DESKTOP_PACKAGES_READY',flush=True)
