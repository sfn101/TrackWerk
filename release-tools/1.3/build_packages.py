#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic TrackWerk 1.3 release-candidate repackaging; no cards or services are touched.
Programs default to hash-verified Claude c9e3f5a artifacts, not a claimed independent rebuild.
Override programs with --programs-json (path/source_commit/sha256 for each profile).
"""
import argparse,hashlib,json,os,shutil,stat,subprocess,sys,zipfile
from pathlib import Path,PurePosixPath
STAMP=(2026,11,20,0,0,0);VERSION='1.3.0';COMMIT='c9e3f5ac11740ebf949de394915f3333322efa9e'
HERE=Path(__file__).resolve().parent;SOURCE=HERE.parents[1];WORK=SOURCE.parent
DEFAULT_CLAUDE=Path(os.environ.get('TRACKWERK_RELEASE_INPUTS',str(WORK/'release-inputs')))
DEFAULT_TEMPLATES=Path(os.environ.get('TRACKWERK_RELEASE_TEMPLATES',str(WORK/'previous-packages')))

def sha(data):return hashlib.sha256(data).hexdigest()
def jb(value):return (json.dumps(value,indent=2,ensure_ascii=False)+'\n').encode()
def safe(path):
    p=Path(os.path.abspath(path))
    if any(q.is_symlink() for q in [p,*p.parents]):raise RuntimeError('Linked input/output refused: '+str(p))
    return p

def unzip(path):
    result={};modes={}
    with zipfile.ZipFile(path) as z:
        for i in z.infolist():
            if i.is_dir():continue
            rel=PurePosixPath(i.filename)
            if rel.is_absolute() or '..' in rel.parts or '\\' in str(rel) or i.filename.casefold() in {n.casefold() for n in result}:raise RuntimeError('Unsafe/duplicate archive member')
            if stat.S_IFMT(i.external_attr>>16)==stat.S_IFLNK:raise RuntimeError('Symlink payload refused')
            if any(p.startswith('.') for p in rel.parts):continue
            result[i.filename]=z.read(i);modes[i.filename]=stat.S_IMODE(i.external_attr>>16) or 0o644
    return result,modes

def checksum_file(files,prefix=''):
    return ''.join(sha(data)+'  '+name[len(prefix):]+'\n' for name,data in sorted(files.items()) if name.startswith(prefix) and name!=prefix+'SHA256SUMS.txt').encode()

def pack(out,files,modes,origin,manifest=None):
    files=dict(files);modes=dict(modes)
    # Nested inherited hashes must never describe an earlier program.
    for name in list(files):
        if name.endswith('/SHA256SUMS.txt'):files[name]=checksum_file(files,name[:-len('SHA256SUMS.txt')])
    files['PROVENANCE.json']=jb(origin)
    if manifest is not None:files['PACKAGE-MANIFEST.json']=jb(manifest)
    files['SHA256SUMS.txt']=checksum_file(files)
    with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for name,data in sorted(files.items()):
            i=zipfile.ZipInfo(name,STAMP);i.create_system=3;i.compress_type=zipfile.ZIP_DEFLATED
            mode=modes.get(name,0o755 if name.endswith(('.py','.sh','.port','.aarch64','.armv7')) else 0o644)
            i.external_attr=(stat.S_IFREG|mode)<<16;z.writestr(i,data)
    value={'file':out.name,'bytes':out.stat().st_size,'sha256':sha(out.read_bytes()),'members':len(files),'origin':origin}
    print(out.name,len(files),round(value['bytes']/1e6,1),'MB',flush=True);return value

def verified_inputs(claude):
    base=claude/'release-1.3.0-pakete-2026-11-20';sums={}
    for line in (base/'SHA256SUMS.txt').read_text().splitlines():
        h,name=line.split(None,1);sums[name.strip().removeprefix('./') if hasattr(str,'removeprefix') else name.strip().lstrip('./')]=h
    def get(name):
        path=base/name;data=path.read_bytes()
        if sums.get(name)!=sha(data):raise RuntimeError('Input checksum mismatch: '+name)
        return data
    updates={}
    for key,name in [('brick','Brick'),('miyoo','Miyoo'),('rgb20s','RGB20S-AmberELEC')]:
        filename='Programm-Updates/TrackWerk-1.3.0-c9e3f5a-'+name+'-Program-Update-TEST.zip'
        get(filename);updates[key]=unzip(base/filename)
    return base,get,updates

def additions(claude,manual_dir):
    files={}
    for name,expected in [('TrackWerk-Sample-Pack-1.3',800),('TrackWerk-Drum-Machines-Samples',728)]:
        folder=claude/'sample-pack/ausgabe'/name;count=0
        for p in sorted(folder.rglob('*')):
            if p.is_file() and not any(part.startswith('.') for part in p.relative_to(folder).parts):
                data=p.read_bytes();files['Samples/'+name+'/'+p.relative_to(folder).as_posix()]=data
                if p.suffix.lower()=='.wav':count+=1
        if count!=expected:raise RuntimeError('Sample count mismatch: '+name+' '+str(count))
    for filename in ['TrackWerk-1.3-Manual-EN.pdf','TrackWerk-1.3-Handbuch-DE.pdf']:
        data=(manual_dir/filename).read_bytes()
        if not data.startswith(b'%PDF'):raise RuntimeError('Invalid PDF')
        files['Handbuch-Manual/'+filename]=data
    files['Handbuch-Manual/TRANSLATION-NOTE.txt']=b'The German reference manual is machine translated. The English text is the reference. Both include a beginner quick start. / Das deutsche Referenzhandbuch ist maschinell uebersetzt.\n'
    return files

def origin(profile,programs,template=None,physical=False):
    return {'product':'TrackWerk','version':VERSION,'profile':profile,'production':'independently rebuilt programs with verified release packaging' if programs and all(v.get('independently_rebuilt') for v in programs.values()) else 'verified release packaging','programs':programs,'base_program_hardware_run_reported_by':'Kid Bunka, 20 November 2026' if physical else None,'final_program_hardware_tested':False,'base_program_source_commit':COMMIT,'new_package_layout_hardware_tested':False,'community_test':not physical,'template':template,'published':False}

def notice(files,prefix):
    for rel in ['LICENSE','CREDITS.md']:
        p=SOURCE/'source'/rel
        if p.exists():files[prefix+'licenses/'+rel]=p.read_bytes()
    p=SOURCE/'source/docs/licenses'
    if p.exists():
        for q in p.rglob('*'):
            if q.is_file():files[prefix+'licenses/'+q.relative_to(p).as_posix()]=q.read_bytes()
    p=SOURCE/'docs/THIRD-PARTY-CHIMERA.md'
    if p.exists():files[prefix+'licenses/THIRD-PARTY-CHIMERA.md']=p.read_bytes()

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--claude-root',type=Path,default=DEFAULT_CLAUDE);parser.add_argument('--templates',type=Path,default=DEFAULT_TEMPLATES);parser.add_argument('--output',type=Path,default=WORK/'packages');parser.add_argument('--manual-dir',type=Path);parser.add_argument('--programs-json',type=Path);parser.add_argument('--android-apk-json',type=Path);parser.add_argument('--r36ultra-deb-json',type=Path);parser.add_argument('--only',nargs='*');a=parser.parse_args()
    claude=safe(a.claude_root);templates=safe(a.templates);out=safe(a.output);out.mkdir(parents=True,exist_ok=True)
    base,get,updates=verified_inputs(claude)
    programs={'brick':updates['brick'][0]['trackwerk.aarch64'],'rgb20s':updates['rgb20s'][0]['trackwerk.aarch64'],'miyoo-mini':updates['miyoo'][0]['trackwerk-offline.armv7'],'miyoo-plus':updates['miyoo'][0]['trackwerk-plus-online.armv7']}
    program_meta={k:{'sha256':sha(v),'bytes':len(v),'source_commit':COMMIT,'independently_rebuilt':False} for k,v in programs.items()}
    if a.programs_json:
        for key,value in json.loads(safe(a.programs_json).read_text()).items():
            data=safe(value['path']).read_bytes()
            if sha(data)!=value['sha256']:raise RuntimeError('Override program checksum mismatch')
            programs[key]=data;program_meta[key]={'sha256':sha(data),'bytes':len(data),'source_commit':value['source_commit'],'independently_rebuilt':True}
    for key,data in programs.items():
        bits,machine=(1,40) if key.startswith('miyoo') else (2,183)
        if data[:5]!=b'\x7fELF'+bytes([bits]) or int.from_bytes(data[18:20],'little')!=machine:raise RuntimeError('Wrong program architecture: '+key)
    manuals=safe(a.manual_dir or base/'Handbuch-Manual');extras=additions(claude,manuals)
    cover=(SOURCE/'source/shell/brick/menu-cover-trackwerk-1.3.0.png').read_bytes()
    box=(SOURCE/'source/shell/miyoo/boxart.png').read_bytes()
    commits=sorted({v['source_commit'] for v in program_meta.values()});program_sources=commits[0] if len(commits)==1 else '; '.join(k+' '+v['source_commit'] for k,v in sorted(program_meta.items()))
    metadata={'version':VERSION,'program_source_commit':program_sources,'programs':program_meta,'cover_sha256':sha(cover),'miyoo_cover_sha256':sha(box)}
    spec=jb(metadata);helper=(HERE/'release_metadata.py').read_bytes()
    entries=[]
    def wanted(key):return not a.only or key in a.only
    def emit(name,files,modes,profile,keys,template=None,physical=False):
        entries.append(pack(out/name,files,modes,origin(profile,{k:program_meta[k] for k in keys},template,physical)))
    def common(files,readme,mode='first'):
        files['START-HERE.txt']=readme.encode();files['SOURCE.txt']=('TrackWerk 1.3.0 corresponding source: TrackWerk-1.3.0-Source.zip\nProgram code: '+program_sources+'\nPrograms independently rebuilt when marked in PROVENANCE.json. Previous base programs were device-tested by Kid Bunka; final FM-bounds correction and new install layout were software-tested only.\n').encode()
        files['release_metadata.py']=helper;files['RELEASE-METADATA.json']=spec;files['menu-cover-trackwerk-1.3.0.png']=cover;files['boxart-miyoo-1.3.0.png']=box
    # Reuse the existing robust program transaction and fresh volume attestation.
    mfiles,mmodes=updates['miyoo'];mfiles=dict(mfiles);mmodes=dict(mmodes)
    mfiles['trackwerk-offline.armv7']=programs['miyoo-mini'];mfiles['trackwerk-plus-online.armv7']=programs['miyoo-plus']
    manifest=json.loads(mfiles['UPDATE-MANIFEST.json']);manifest['version']=VERSION
    for key,target in [('miyoo-mini','Roms/PORTS/Games/TrackWerk/trackwerk'),('miyoo-plus','Roms/PORTS/Games/TrackWerkPlus/trackwerk')]:
        row=manifest['programs'][target];old=row['sha256'];row.update(sha256=sha(programs[key]),bytes=len(programs[key]))
        row['previous_sha256s']=list(dict.fromkeys([old]+row['previous_sha256s']));row['previous_sha256s']=[h for h in row['previous_sha256s'] if h!=row['sha256']]
    mfiles['UPDATE-MANIFEST.json']=jb(manifest)
    updater=mfiles['update_miyoo.py'].decode().replace("VERSION = '1.2.1'","VERSION = '1.3.0'")
    needle='    onion = regular(card / \'.tmp_update/onionVersion/version.txt\').read_text().strip()'
    preflight="    for rel in ['.tmp_update', '.tmp_update/script/stop_audioserver.sh', 'miyoo/app/MainUI', 'Emu/PORTS/launch_standalone.sh']:\n        if not (checked(card / rel).is_dir() if rel == '.tmp_update' else checked(card / rel).is_file()):\n            raise UpdateError('Incomplete Onion installation; no files changed: ' + rel)\n"
    updater=updater.replace(needle,preflight+needle,1)
    needle="    result['existing_user_files_modified'] = False\n    return result"
    updater=updater.replace(needle,"    result['existing_user_files_modified'] = False\n    if apply:\n        import release_metadata\n        result['menu_metadata'] = release_metadata.refresh(card, device, bundle, write=True)\n    return result")
    mfiles['update_miyoo.py']=updater.encode()
    common(mfiles,'TRACKWERK 1.3.0 / MIYOO PROGRAM UPDATE\nPower off the handheld. Unzip on your computer. Back up the card first.\nPlan: python3 update_miyoo.py --card /absolute/CARD --device miyoo-mini\nUse miyoo-plus for a Plus. Repeat with --apply to install verified programs and refresh the 1.3 menu names, cover and description.\nRequires an intact Onion installation, Ports Collection, audio handover and MainUI. Existing music/preferences are retained. Previous programs and menu files are backed up.\nSamples/PDF manuals: download the Miyoo Samples-Handbuch-Menuebild ZIP and copy its contents only when a destination file is missing.\nKid Bunka ran the preceding base program. This final FM-bounds rebuild and update transaction were software-tested, not physically retested.\n')
    if wanted('miyoo-update'):emit('TrackWerk-1.3.0-Miyoo-Program-Update.zip',mfiles,mmodes,'miyoo-update',['miyoo-mini','miyoo-plus'],physical=True)
    # Standalone program updates for Brick and AmberELEC.
    for key,label,script in [('brick','Brick','update_brick.py'),('rgb20s','RGB20S-AmberELEC','update_amberelec.py')]:
        files,modes=updates[key];files=dict(files);files['trackwerk.aarch64']=programs[key]
        m=json.loads(files['update-manifest.json']);m.update(version=VERSION,bytes=len(programs[key]),sha256=sha(programs[key]),source_commit=program_meta[key]['source_commit']);files['update-manifest.json']=jb(m)
        text=files[script].decode()
        needle='        print(json.dumps(result, indent=2))'
        parent='args.target.parents[2]' if key=='brick' else 'args.target.parents[1]'
        condition="args.rollback is None" if key=="brick" else "args.apply"
        inject="        if "+condition+":\n            import release_metadata\n            result['menu_metadata'] = release_metadata.refresh("+parent+", '"+key+"', args.package, write=True)\n"
        if needle not in text:raise RuntimeError('Unexpected update-script template')
        files[script]=text.replace(needle,inject+needle).encode()
        target='roms/ports/trackwerk' if key=='brick' else 'ports/trackwerk-amberelec'
        common(files,'TRACKWERK 1.3.0 / '+label+' PROGRAM UPDATE\nPower off. Back up the card. Unzip this package on your computer.\nRun python3 '+script+' --target /absolute/CARD/'+target+(' --apply' if key=='rgb20s' else '')+'\nThe existing updater verifies the program, retains rollback copies and updates the version, cover and menu description. Songs, Samples and preferences are retained.\nFor Samples and EN/DE manuals use the matching optional Samples-Handbuch ZIP. These packages were simulated; Kid Bunka ran the preceding base program. The final FM-bounds correction was software-tested.\n')
        if wanted(key+'-update'):emit('TrackWerk-1.3.0-'+label+'-Program-Update.zip',files,modes,key+'-update',[key],physical=True)
    # Onion first installs: retain proven library/tool payloads; replace only exact program and metadata.
    for key,label,app in [('miyoo-mini','Miyoo-Mini-Onion','TrackWerk'),('miyoo-plus','Miyoo-Plus-Onion','TrackWerkPlus')]:
        if not wanted(key):continue
        template='TrackWerk-1.2.1-'+label+'-First-Install.zip';files,modes=unzip(templates/template)
        prefix='Card-Payload/Roms/PORTS/Games/'+app+'/'
        files[prefix+'trackwerk']=programs[key];modes[prefix+'trackwerk']=0o755
        # Rename stale 1.1 shortcut/image filenames and all release labels.
        for name in list(files):
            if name.endswith('.py') or name.startswith('Factory/'):continue
            if name.endswith(('.txt','.json','.sh','.port')) and name!='FACTORY-MANIFEST.json':
                files[name]=files[name].replace(b'1.2.1',b'1.3.0').replace(b'TrackWerk 1.1',b'TrackWerk 1.3')
            new=name.replace('TrackWerk 1.1.', 'TrackWerk 1.3.')
            if new!=name:files[new]=files.pop(name);modes[new]=modes.pop(name,0o644)
        cfg='Card-Payload/App/'+app+'/config.json';value=json.loads(files[cfg]);value['label']='TrackWerk 1.3.0';value['description']='TrackWerk 1.3.0 by Kid Bunka / '+key;files[cfg]=jb(value)
        files['Card-Payload/App/'+app+'/icon.png']=box;files['Card-Payload/Roms/PORTS/Imgs/TrackWerk 1.3.png']=box
        for old in ['EXPERIMENTAL-STATUS.json','START-HIER-DE.txt','START-HERE.txt']:files.pop(prefix+old,None)
        files[prefix+'TEST-STATUS.json']=jb({'base_program_user_tested':True,'program_user_tested':False,'new_package_hardware_tested':False,'device':key})
        files[prefix+'VERSION.txt']=b'TrackWerk 1.3.0\n';notice(files,prefix)
        for name,data in extras.items():files['Shared-Data/'+name]=data
        for name in list(files):
            if name.endswith('/SHA256SUMS.txt'):files[name]=checksum_file(files,name[:-len('SHA256SUMS.txt')])
        payload=[]
        for name,data in sorted(files.items()):
            if name.startswith('Card-Payload/'):payload.append({'path':name[len('Card-Payload/'):],'bytes':len(data),'sha256':sha(data),'mode':modes.get(name,0o755 if name.endswith(('.sh','.port')) else 0o644)})
        pm=json.loads(files['PROGRAM-MANIFEST.json']);pm.update(version=VERSION,files=payload);files['PROGRAM-MANIFEST.json']=jb(pm)
        shared=[{'path':name[len('Shared-Data/'):],'bytes':len(data),'sha256':sha(data)} for name,data in sorted(files.items()) if name.startswith('Shared-Data/')]
        files['SHARED-MANIFEST.json']=jb({'files':shared})
        installer=files['install_miyoo.py'].decode().replace('1.2.1','1.3.0').replace('TrackWerk 1.3.0.png','TrackWerk 1.3.png').replace('TrackWerk 1.3.0.port','TrackWerk 1.3.port')
        installer=installer.replace("    before = protected(home); changes = []; required = 0", "    shared = json.loads((package / 'SHARED-MANIFEST.json').read_text())['files']\n    for row in shared:\n        source_data(package / 'Shared-Data', row)\n        checked(home, row['path'])\n    before = protected(home); changes = []; required = sum(row['bytes'] for row in shared if not checked(home,row['path']).exists())")
        installer=installer.replace("        for rel, value in before.items():", "        shared_added = 0\n        for row in shared:\n            gate(); target = checked(home, row['path'])\n            if target.exists():\n                if not target.is_file(): raise RuntimeError('Shared data target is not a file')\n                continue\n            copy_new(source_data(package / 'Shared-Data', row), target); shared_added += 1\n        report['added_samples_and_manuals'] = shared_added\n        for rel, value in before.items():")
        installer=installer.replace("    mount_device = card.stat().st_dev; onion_version = version.read_text().strip()", "    if not checked(card, 'miyoo/app/MainUI').is_file(): raise RuntimeError('Onion MainUI is missing; no card files changed')\n    mount_device = card.stat().st_dev; onion_version = version.read_text().strip()")
        files['install_miyoo.py']=installer.encode()
        common(files,'TRACKWERK 1.3.0 / '+label+' FIRST INSTALL\nFor an existing working Onion card (this does not install Onion). Power off and back up the card.\nUnzip on the computer. Plan: python3 install_miyoo.py --card /absolute/CARD\nRepeat with --apply. The installer adds the program, 2,432 factory presets, 800 TrackWerk samples, 728 drum-machine samples and EN/DE PDF manuals.\nYour TrackWerk data folder is TrackWerk/ at the card root. Only missing samples/presets/manuals are added; existing songs/settings stay in place.\nFor an older installed TrackWerk use the Program Update instead, then the Samples ZIP.\nBase program tested by Kid Bunka; final rebuilt program and new package layout were software-tested only. German reference manual: machine translated.\n')
        emit('TrackWerk-1.3.0-'+label+'-First-Install.zip',files,modes,key,[key],template,True)
    # AmberELEC first installation, with a card-root payload and no personal files.
    if wanted('rgb20s'):
        template='TrackWerk-1.2.1-RGB20S-AmberELEC.zip';files,modes=unzip(templates/template);prefix='ports/trackwerk-amberelec/'
        files.pop('LIES-MICH.txt',None)
        files[prefix+'trackwerk.aarch64']=programs['rgb20s'];notice(files,prefix)
        for name,data in extras.items():files[prefix+'data/'+name]=data
        files[prefix+'menu-cover-trackwerk-1.3.0.png']=cover;files[prefix+'VERSION.txt']=b'TrackWerk 1.3.0\n'
        files[prefix+'gameinfo.xml']=('<game><name>TrackWerk 1.3.0</name><desc>TrackWerk 1.3.0 by Kid Bunka, with 128 synth models and Sample Pack.</desc><image>./menu-cover-trackwerk-1.3.0.png</image></game>\n').encode()
        files['MANIFEST.json']=jb({'version':VERSION,'device':'RGB20S / AmberELEC','program_sha256':sha(programs['rgb20s']),'base_program_hardware_run_by_Kid_Bunka':True,'final_program_hardware_tested':False,'new_package_hardware_tested':False,'samples':1528,'private_user_data_included':False})
        common(files,'TRACKWERK 1.3.0 / RGB20S AMBERELEC FIRST INSTALL\nPower off. Back up your games card. Extract ports/ to the games-card root.\nBefore first launch, python3 release_metadata.py --card /absolute/CARD --device rgb20s --apply sets the menu image and description.\nIncludes Samples and EN/DE manuals in ports/trackwerk-amberelec/data/.\nIf an older shared folder roms/ports/trackwerk/data exists, the launcher retains that home: copy only missing Samples and Handbuch-Manual files there from the new data folder.\nExisting installation: use Program Update; never replace your data folder.\nBase program tested by Kid Bunka; final rebuilt program and new first-install layout were not physically tested. German reference manual is machine translated.\n')
        emit('TrackWerk-1.3.0-RGB20S-AmberELEC.zip',files,modes,'rgb20s',['rgb20s'],template,True)
    # Existing Claude all-in-one KNULLI/R36S payloads are verified and deterministically repackaged.
    for key,filename,newname in [('brick','TrackWerk-1.3.0-Brick-KNULLI-First-Install.zip',None),('h700','TrackWerk-1.3.0-Anbernic-H700-KNULLI-First-Install.zip','TrackWerk-1.3.0-Anbernic-H700-KNULLI-Community-Test.zip'),('r36s','TrackWerk-1.3.0-R36S-First-Install.zip','TrackWerk-1.3.0-R36S-Community-Test.zip')]:
        if not wanted(key):continue
        get(filename);files,modes=unzip(base/filename);files['trackwerk/trackwerk.aarch64']=programs['brick'];notice(files,'trackwerk/')
        for name in list(files):
            if name.startswith(('trackwerk/data/Samples/','trackwerk/docs/')):del files[name]
        for name,data in extras.items():
            target='trackwerk/docs/'+name[len('Handbuch-Manual/'):] if name.startswith('Handbuch-Manual/') else 'trackwerk/data/'+name
            files[target]=data
        text=files['START-HERE.txt'].decode().replace('Not reviewed by a second person yet.','Repackaged with verified input checksums; this package layout has not been physically tested.')
        files['START-HERE.txt']=(text+'\nFinal rebuild note: Kid Bunka tested the preceding base program on supported devices. This package contains a later FM velocity bounds fix; that final correction and the new layout were software-tested only.\n').encode();files['release_metadata.py']=helper;files['RELEASE-METADATA.json']=spec;files['menu-cover-trackwerk-1.3.0.png']=cover
        emit(newname or filename,files,modes,key,['brick'],filename,key=='brick')
    if wanted('r36ultra'):
        filename='TrackWerk-1.3.0-R36-Ultra-dArkOSRE-Community-Test.zip';get(filename);files,modes=unzip(base/filename)
        if a.r36ultra_deb_json:
            value=json.loads(safe(a.r36ultra_deb_json).read_text());data=safe(value['path']).read_bytes()
            if sha(data)!=value['sha256'] or not data.startswith(b'!<arch>\n'):raise RuntimeError('Invalid replacement Debian package')
            members=[n for n in files if n.endswith('_arm64.deb')]
            if len(members)!=1:raise RuntimeError('Expected exactly one arm64 Debian payload')
            files[members[0]]=data;files['DEBIAN-PROVENANCE.json']=jb({'sha256':sha(data),'source_commit':value['source_commit'],'device_tested':False})
        files['TEST-STATUS.txt']=b'COMMUNITY TEST ONLY. Not run on an R36 Ultra by Codex or Kid Bunka. Debian package depends on SDL >= 2.32.4. Dependency installation and actual dArkOSRE startup remain unverified.\n'
        emit(filename,files,modes,'r36ultra',[],filename,False)
    if wanted('portmaster'):
        # Build an autoinstall-compatible aarch64 payload using the existing original launcher.
        template='TrackWerk-1.2.1-Brick-KNULLI-First-Install.zip';old,modes=unzip(templates/template);files={};newmodes={}
        for name,data in old.items():
            if name.startswith('trackwerk/') and name.split('/')[1] in ['factory','fonts','tools','licenses']:files['pockettracker/'+name[len('trackwerk/'):]]=data
        for name,data in list(files.items()):
            if name.startswith('pockettracker/factory/Presets/'):
                files['pockettracker/data/Presets/'+name[len('pockettracker/factory/Presets/'):]]=data
        files['pockettracker/pockettracker.aarch64']=programs['brick'];newmodes['pockettracker/pockettracker.aarch64']=0o755
        files['TrackWerk.sh']=(SOURCE/'source/shell/portmaster/TrackWerk.sh').read_bytes();notice(files,'pockettracker/')
        for name,data in extras.items():files['pockettracker/data/'+name]=data
        files['pockettracker/menu-cover-trackwerk-1.3.0.png']=cover
        files['pockettracker/gameinfo.xml']=b'<game><name>TrackWerk 1.3.0</name><desc>Free eight-track music studio with 128 synth models and Sample Pack. Community test on unlisted devices.</desc><image>./menu-cover-trackwerk-1.3.0.png</image></game>\n'
        pm=json.loads((SOURCE/'source/shell/portmaster/port.json').read_text());pm['name']='TrackWerk-1.3.0-PortMaster.zip'
        pm['attr'].update(title='TrackWerk 1.3.0',desc='Eight-track music studio by Kid Bunka. 128 synth models, Froggy, sampling and 1,528 samples.',inst='Use PortMaster autoinstall. aarch64 only; this new package is a community test on unlisted devices.',exp=True)
        pm['attr']['porter']=list(dict.fromkeys(pm['attr'].get('porter',[])+['Kid Bunka']))
        files['pockettracker/port.json']=jb(pm)
        files['START-HERE.txt']=b'TrackWerk 1.3.0 / PortMaster aarch64 community candidate\nCopy ZIP to roms/ports/PortMaster/autoinstall and run PortMaster. Requires aarch64 CFW with system SDL >= 2.0.18. Samples/manuals included under pockettracker/data/. Program is the verified Brick aarch64 build; compatibility on other PortMaster systems is untested. Please report model/OS/log.txt.\n'
        emit('TrackWerk-1.3.0-PortMaster.zip',files,newmodes,'portmaster',['brick'],template,False)
    # Separate existing-user additions; missing-only copying is required for user data.
    for key,name,prefix in [('miyoo-extras','Miyoo-Samples-Handbuch-Menuebild','TrackWerk'),('rgb20s-extras','RGB20S-Samples-Handbuch-Menuebild','ports/trackwerk-amberelec/data'),('android-extras','Android-Samples-Handbuch','TrackWerk')]:
        if not wanted(key):continue
        files={prefix+'/'+n:d for n,d in extras.items()}
        if key=='miyoo-extras':files['Roms/PORTS/Imgs/TrackWerk 1.3.png']=box;files['Roms/PORTS/Imgs/TrackWerk 1.3 Offline.png']=box
        if key=='rgb20s-extras':files['ports/trackwerk-amberelec/menu-cover-trackwerk-1.3.0.png']=cover
        files['install_samples.py']=(HERE/'install_samples.py').read_bytes()
        files['CONTENT-MANIFEST.json']=jb({'files':[{'path':n,'source':prefix+'/'+n,'bytes':len(d),'sha256':sha(d)} for n,d in sorted(extras.items())]})
        files['START-HERE.txt']=('TrackWerk 1.3.0 samples and EN/DE manuals\nSafe computer-side installation: unzip the complete package, run python3 install_samples.py --data-home /absolute/PATH/TO/YOUR/EXISTING/DATA\nThe command only shows a plan. Repeat with --apply to add missing files; existing samples/projects/preferences are NEVER overwritten.\nAndroid: select the existing folder granted to the app, or copy missing files from '+prefix+'/ on the phone. APK updates in place; do not uninstall.\nMiyoo: current installs use /TrackWerk; legacy cards may still use /PocketTrackerExtreme. Use the existing data home.\nRGB20S: an existing roms/ports/trackwerk/data shared home takes precedence over ports/trackwerk-amberelec/data.\n800 TrackWerk samples + 728 drum-machine samples. Music use, commercial or noncommercial, is allowed; no sample-pack resale. Full licence in each folder. German reference manual machine translated.\n').encode()
        emit('TrackWerk-1.3.0-'+name+'.zip',files,{},key,[],physical=True)
    if wanted('android'):
        value=json.loads(safe(a.android_apk_json).read_text()) if a.android_apk_json else None
        data=safe(value['path']).read_bytes() if value else get('TrackWerk-1.3.0-Android-Update.apk')
        if value and (sha(data)!=value['sha256'] or not value['signer_matches_previous']):raise RuntimeError('APK signature provenance mismatch')
        target=out/'TrackWerk-1.3.0-Android-Update.apk';target.write_bytes(data)
        entries.append({'file':target.name,'bytes':len(data),'sha256':sha(data),'origin':{'production':'independently rebuilt, signed and verified APK' if value else 'unchanged verified signed APK','source_commit':value['source_commit'] if value else COMMIT,'base_hardware_test_reported':True,'final_program_hardware_tested':False if value else True,'version_code':103000}})
    if wanted('samples'):
        for name in ['TrackWerk-Sample-Pack-1.3.zip','TrackWerk-Drum-Machines-Samples.zip']:
            data=(claude/'sample-pack/ausgabe'/name).read_bytes();target=out/name;target.write_bytes(data);entries.append({'file':name,'bytes':len(data),'sha256':sha(data),'origin':{'production':'unchanged Kid Bunka sample pack'}})
        for name in ['SDL2-mmiyoo-f6319cf9-source.tar.gz','TrackWerk-1.0-Factory-Presets.zip','TrackWerk-1.2.0-Demo-Projects.zip']:
            data=(templates/name).read_bytes();(out/name).write_bytes(data);entries.append({'file':name,'bytes':len(data),'sha256':sha(data),'origin':{'production':'unchanged corresponding-source/factory/demo dependency'}})
    report=WORK/'reports/package-provenance.json';report.parent.mkdir(exist_ok=True)
    previous=json.loads(report.read_text()) if report.exists() else {'artifacts':[]}
    by={x['file']:x for x in previous['artifacts']};by.update({x['file']:x for x in entries})
    result={'version':VERSION,'programs':program_meta,'scope':'local deterministic repackaging and simulation; no device/card/publication writes','artifacts':sorted(by.values(),key=lambda x:x['file'])};report.write_bytes(jb(result))
    (out/'SHA256SUMS-PACKAGES.txt').write_text(''.join(x['sha256']+'  '+x['file']+'\n' for x in result['artifacts']))
    print('PACKAGES',len(result['artifacts']),flush=True)
if __name__=='__main__':main()
