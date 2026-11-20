#!/usr/bin/env python3
"""Meaningful package/transaction simulations on local ordinary directories, never mounted cards."""
import hashlib,importlib.util,json,os,shutil,subprocess,sys,uuid,zipfile
from pathlib import Path
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2];OUT=ROOT/'packages';REPORT=ROOT/'reports';FIX=REPORT/'package-simulation'
OLD=Path(os.environ.get('TRACKWERK_PREVIOUS_PACKAGES',str(ROOT/'previous-packages')))
results=[]
def sha(b):return hashlib.sha256(b).hexdigest()
def check(name,fn):
 try: detail=fn();results.append({'name':name,'passed':True,'detail':detail});print('PASS',name,flush=True)
 except BaseException as e:results.append({'name':name,'passed':False,'error':repr(e)});print('FAIL',name,repr(e),flush=True);raise

def load(name,path):
 sys.path.insert(0,str(path.parent));spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def extract(name,folder):
 p=FIX/folder;p.mkdir(parents=True,exist_ok=True)
 with zipfile.ZipFile(OUT/name) as z:z.extractall(p)
 return p

def put(root,rel,data):
 p=root/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);return p

def onion(card,plus=False):
 for name,data in [('.tmp_update/onionVersion/version.txt',b'4.5.0'),('.tmp_update/script/stop_audioserver.sh',b'#!/bin/sh\n'),('miyoo/app/MainUI',b'fixture'),('Emu/PORTS/launch_standalone.sh',b'#!/bin/sh\n')]:put(card,name,data)
 if plus:put(card,'.tmp_update/lib/libcurl.so.4',b'fixture')

def snap(root):
 return {p.relative_to(root).as_posix():sha(p.read_bytes()) for p in root.rglob('*') if p.is_file()}

def install_test(key,label):
 package=extract('TrackWerk-1.3.0-'+label+'-First-Install.zip','install-'+key);card=FIX/('card-first-'+key);card.mkdir();onion(card,key=='plus')
 put(card,'TrackWerk/settings.json',b'{"volume":77}');put(card,'TrackWerk/Projects/KEEP.ptp',b'private song');put(card,'TrackWerk/Samples/KEEP.wav',b'private wave')
 factory=json.loads((package/'FACTORY-MANIFEST.json').read_text())['files'];put(card,'TrackWerk/Presets/'+factory[0]['path'],b'user-edited factory preset')
 shared=json.loads((package/'SHARED-MANIFEST.json').read_text())['files'];sample=next(r['path'] for r in shared if r['path'].endswith('.wav'));put(card,'TrackWerk/'+sample,b'user-edited existing same-name sample')
 protected=snap(card/'TrackWerk');m=load('installer_'+key,package/'install_miyoo.py');before=snap(card);plan=m.install(card,package,False);assert before==snap(card)
 report=m.install(card,package,True)
 for name,h in protected.items():assert sha((card/'TrackWerk'/name).read_bytes())==h
 for row in shared:assert (card/'TrackWerk'/row['path']).is_file()
 assert (card/'Roms/PORTS/Shortcuts/TrackWerk 1.3.port').exists()
 assert not (card/'Roms/PORTS/Shortcuts/TrackWerk 1.1.port').exists()
 repeat=m.install(card,package,True);assert repeat['changed_program_files']==0 and repeat['added_samples_and_manuals']==0
 return {'installed_shared':report['added_samples_and_manuals'],'preserved_private_files':len(protected),'dry_run_read_only':True,'idempotent':True}

def failure_preflight():
 package=FIX/'install-mini';card=FIX/'card-broken-onion';card.mkdir();put(card,'.tmp_update/onionVersion/version.txt',b'4.5.0');put(card,'TrackWerk/Projects/KEEP.ptp',b'private song');before=snap(card)
 m=load('installer_broken',package/'install_miyoo.py')
 try:m.install(card,package,True)
 except RuntimeError as e:assert 'MainUI' in str(e)
 else:raise AssertionError('incomplete Onion accepted')
 assert snap(card)==before;return 'Missing MainUI rejected before any writes'

def update_test():
 package=extract('TrackWerk-1.3.0-Miyoo-Program-Update.zip','update-miyoo');card=FIX/'card-update';card.mkdir();onion(card,True)
 with zipfile.ZipFile(OLD/'TrackWerk-1.2.1-Miyoo-Program-Update.zip') as z:
  for app,fn in [('TrackWerk','trackwerk-offline.armv7'),('TrackWerkPlus','trackwerk-plus-online.armv7')]:put(card,'Roms/PORTS/Games/'+app+'/trackwerk',z.read(fn))
 for app in ['TrackWerk','TrackWerkPlus']:
  put(card,'App/'+app+'/config.json',b'{"label":"TrackWerk 1.1"}');put(card,'App/'+app+'/launch.sh',b'bad old port link')
  put(card,'Roms/PORTS/Games/'+app+'/launch.sh',b'#!/bin/sh\n')
  suffix=' Offline' if app=='TrackWerk' else ''
  put(card,'Roms/PORTS/Shortcuts/TrackWerk 1.1'+suffix+'.port',('GameDir="'+app+'"\n').encode())
 put(card,'Roms/PORTS/Shortcuts/Shortcuts_cache6.db',b'disposable cache');put(card,'Roms/PORTS/Shortcuts/OtherGame.port',b'GameDir="OtherGame"\n')
 for rel,data in [('TrackWerk/settings.json',b'keep prefs'),('TrackWerk/Projects/PRIVATE.ptp',b'keep song'),('TrackWerk/Samples/PRIVATE.wav',b'keep sample')]:put(card,rel,data)
 before_private=snap(card/'TrackWerk');m=load('update_miyoo_13',package/'update_miyoo.py')
 identity={'mount':str(card),'volume_uuid':str(uuid.uuid4()),'filesystem':'msdos','st_dev':card.stat().st_dev,'onion_version':'4.5.0','externally_verified':True}
 guard=lambda c,expected: c==card and expected==identity
 before=snap(card);plan=m.run_bundle(card,package,'miyoo-plus',identity,guard,False);assert snap(card)==before
 result=m.run_bundle(card,package,'miyoo-plus',identity,guard,True)
 assert snap(card/'TrackWerk')==before_private
 assert (card/'Roms/PORTS/Shortcuts/TrackWerk 1.3.port').exists() and (card/'Roms/PORTS/Shortcuts/TrackWerk 1.3 Offline.port').exists()
 assert (card/'Roms/PORTS/Imgs/TrackWerk 1.3.png').read_bytes()==(package/'boxart-miyoo-1.3.0.png').read_bytes()
 assert not (card/'Roms/PORTS/Shortcuts/TrackWerk 1.1.port').exists();assert not (card/'Roms/PORTS/Shortcuts/Shortcuts_cache6.db').exists()
 assert (card/'Roms/PORTS/Shortcuts/OtherGame.port').read_bytes()==b'GameDir="OtherGame"\n'
 assert list((card/'.TrackWerk-Metadata-Backups').rglob('Shortcuts_cache6.db'))
 repeat=m.run_bundle(card,package,'miyoo-plus',identity,guard,True);assert repeat['status']=='already_current'
 # Missing audio handover rejects an update before changes, including an already-current one.
 (card/'.tmp_update/script/stop_audioserver.sh').unlink();before=snap(card)
 try:m.run_bundle(card,package,'miyoo-plus',identity,guard,True)
 except RuntimeError as e:assert 'audio' in str(e) or 'stop_audioserver' in str(e)
 else:raise AssertionError('incomplete Onion accepted')
 assert snap(card)==before
 return {'updated_programs':result['updated_program_count'],'user_files_preserved':len(before_private),'other_shortcut_preserved':True,'cache_backed_up':True,'idempotent':True,'broken_onion_refused':True}

def rollback_test():
    package=FIX/'update-miyoo';card=FIX/'card-update';onion(card,True)
    old={}
    with zipfile.ZipFile(OLD/'TrackWerk-1.2.1-Miyoo-Program-Update.zip') as z:
        for app,fn in [('TrackWerk','trackwerk-offline.armv7'),('TrackWerkPlus','trackwerk-plus-online.armv7')]:
            path='Roms/PORTS/Games/'+app+'/trackwerk';old[path]=z.read(fn);put(card,path,old[path])
    m=load('update_rollback_13',package/'update_miyoo.py')
    identity={'mount':str(card),'volume_uuid':str(uuid.uuid4()),'filesystem':'msdos','st_dev':card.stat().st_dev,'onion_version':'4.5.0','externally_verified':True}
    guard=lambda c,expected:c==card and expected==identity
    plan=m.select_programs(package,card,'miyoo-plus',identity,json.loads((package/'UPDATE-MANIFEST.json').read_text()))
    private=snap(card/'TrackWerk')
    def fault(index,row):
        if index==1:raise RuntimeError('simulated second-program I/O fault')
    try:m.update(card,plan,True,guard,fault)
    except m.UpdateFailure as e:assert e.receipt['status']=='failed_rolled_back'
    else:raise AssertionError('injected failure did not happen')
    for name,data in old.items():assert (card/name).read_bytes()==data
    assert snap(card/'TrackWerk')==private
    return {'two_original_programs_retained':True,'user_files_retained':True,'fault_after_first_replacement':True}

def samples_test():
    package=extract('TrackWerk-1.3.0-Android-Samples-Handbuch.zip','samples-only');home=FIX/'android-data';home.mkdir()
    rows=json.loads((package/'CONTENT-MANIFEST.json').read_text())['files'];existing=next(r['path'] for r in rows if r['path'].endswith('.wav'));put(home,existing,b'user sample');put(home,'Projects/KEEP.ptp',b'private song');put(home,'settings.json',b'private prefs')
    private=snap(home);m=load('samples_only_13',package/'install_samples.py');before=snap(home);m.install(home,package,False);assert snap(home)==before
    result=m.install(home,package,True)
    for name,value in private.items():assert sha((home/name).read_bytes())==value
    assert m.install(home,package,True)['added_files']==0
    # A corrupt source is rejected before any data writes.
    source=package/rows[-1]['source'];source.write_bytes(b'corrupt');before=snap(home)
    try:m.install(home,package,True)
    except ValueError as e:assert 'checksum' in str(e)
    else:raise AssertionError('corrupt sample accepted')
    assert snap(home)==before
    return {'added_files':result['added_files'],'existing_samples_projects_preferences_preserved':True,'read_only_plan':True,'idempotent':True,'corruption_refused_before_write':True}

def program_cli_test(key):
    label='Brick' if key=='brick' else 'RGB20S-AmberELEC';script='update_brick.py' if key=='brick' else 'update_amberelec.py'
    package=extract('TrackWerk-1.3.0-'+label+'-Program-Update.zip','program-cli-'+key);card=FIX/('card-cli-'+key);card.mkdir()
    rel='roms/ports/trackwerk' if key=='brick' else 'ports/trackwerk-amberelec';app=card/rel
    oldname='TrackWerk-1.2.1-'+label+'-Program-Update.zip'
    with zipfile.ZipFile(OLD/oldname) as z:old=z.read('trackwerk.aarch64')
    put(card,rel+'/trackwerk.aarch64',old);put(card,rel+'/data/Projects/KEEP.ptp',b'private project');put(card,rel+'/data/config.json',b'private prefs')
    before=snap(app/'data');cmd=[sys.executable,str(package/script),'--target',str(app),'--package',str(package)]
    if key=='rgb20s':
        original=snap(card);p=subprocess.run(cmd,capture_output=True,text=True);assert p.returncode==0,(p.stdout,p.stderr);assert snap(card)==original;cmd.append('--apply')
    p=subprocess.run(cmd,capture_output=True,text=True);assert p.returncode==0,(p.stdout,p.stderr)
    assert (app/'trackwerk.aarch64').read_bytes()==(package/'trackwerk.aarch64').read_bytes();assert snap(app/'data')==before
    assert (app/'VERSION.txt').read_text().startswith('TrackWerk 1.3.0');assert list(app.rglob(sha(old)+'.aarch64'))
    return {'actual_cli_executed':True,'new_program_and_rollback_copy_verified':True,'project_preferences_preserved':True,'rgb20s_dry_run_read_only':key=='rgb20s'}

def metadata_test(key):
 package=extract('TrackWerk-1.3.0-'+('Brick' if key=='brick' else 'RGB20S-AmberELEC')+'-Program-Update.zip','metadata-'+key)
 card=FIX/('card-'+key);card.mkdir();prefix='roms/ports/trackwerk' if key=='brick' else 'ports/trackwerk-amberelec';app=card/prefix
 put(card,prefix+'/trackwerk.aarch64',(package/'trackwerk.aarch64').read_bytes());put(card,prefix+'/data/Projects/KEEP.ptp',b'song');put(card,prefix+'/data/config.json',b'prefs')
 put(card,str(Path(prefix).parent/'gamelist.xml'),b'<gameList><game><path>./Other.sh</path><name>Other</name><desc>keep me</desc></game><game><path>./TrackWerk.sh</path><name>TrackWerk 1.2.0</name><desc>old</desc><favorite>true</favorite></game></gameList>')
 m=load('metadata_'+key,package/'release_metadata.py');protected=snap(app/'data');before=snap(card);plan=m.refresh(card,key,package,False);assert before==snap(card)
 result=m.refresh(card,key,package,True);assert protected==snap(app/'data')
 import xml.etree.ElementTree as ET
 root=ET.parse(app.parent/'gamelist.xml').getroot();assert root[0].findtext('name')=='Other' and root[0].findtext('desc')=='keep me';assert root[1].findtext('favorite')=='true';assert root[1].findtext('name')=='TrackWerk 1.3.0' and 'Sample Pack' in root[1].findtext('desc')
 assert m.refresh(card,key,package,True)['metadata']==[]
 # Reject symlink escape before mutation.
 (app/'VERSION.txt').unlink();(app/'VERSION.txt').symlink_to(FIX/'outside-file');before=snap(card)
 try:m.refresh(card,key,package,True)
 except ValueError as e:assert 'link' in str(e).lower()
 else:raise AssertionError('symlink accepted')
 return {'menu_changes':len(result['metadata']),'preserved_unrelated_game_metadata':True,'preserved_project_preferences':True,'idempotent':True,'symlink_refused':True}

def archive_checks():
 manifest=json.loads((REPORT/'package-provenance.json').read_text());count=0
 for row in manifest['artifacts']:
  p=OUT/row['file'];assert sha(p.read_bytes())==row['sha256']
  if p.suffix!='.zip' or row['file'] in ['TrackWerk-1.0-Factory-Presets.zip','TrackWerk-1.2.0-Demo-Projects.zip','TrackWerk-Sample-Pack-1.3.zip','TrackWerk-Drum-Machines-Samples.zip']:continue
  with zipfile.ZipFile(p) as z:
   names=z.namelist();assert len(names)==len(set(n.casefold() for n in names));assert z.testzip() is None
   for line in z.read('SHA256SUMS.txt').decode().splitlines():
    h,n=line.split('  ',1);assert sha(z.read(n))==h,(p.name,n)
   assert 'PROVENANCE.json' in names
   if 'First-Install' in p.name or p.name.endswith('RGB20S-AmberELEC.zip') or p.name.endswith('PortMaster.zip'):
    assert sum(n.lower().endswith('.wav') for n in names)==1528
    assert any(n.endswith('TrackWerk-1.3-Manual-EN.pdf') for n in names) and any(n.endswith('TrackWerk-1.3-Handbuch-DE.pdf') for n in names)
   assert not any('QUELLEN-INTERN' in n or '.keystore' in n or '.jks' in n for n in names)
   count+=1
 return {'verified_repackaged_zips':count,'hashes_and_crcs_verified':True}

if __name__=='__main__':
 if FIX.exists():shutil.rmtree(FIX)
 FIX.mkdir(parents=True)
 try:
  check('Miyoo Mini first install preserves edited files and reruns',lambda:install_test('mini','Miyoo-Mini-Onion'))
  check('Miyoo Plus first install preserves edited files and reruns',lambda:install_test('plus','Miyoo-Plus-Onion'))
  check('Broken Onion first install refuses without writes',failure_preflight)
  check('Miyoo two-program update, menu/cache and read-only plan',update_test)
  check('Two-program update rolls back an injected second-write fault',rollback_test)
  check('Sample installer preserves user files and refuses corruption',samples_test)
  check('Brick program update CLI and rollback copy',lambda:program_cli_test('brick'))
  check('RGB20S program update CLI, dry run and rollback copy',lambda:program_cli_test('rgb20s'))
  check('Brick menu metadata preserves other games and user data',lambda:metadata_test('brick'))
  check('RGB20S menu metadata preserves other games and user data',lambda:metadata_test('rgb20s'))
  check('All candidate archives, hashes, CRCs and payload scope',archive_checks)
 finally:
  report={'scope':'local regular-directory simulations; no physical devices, cards or volume identity claimed','tests':results,'passed':sum(x['passed'] for x in results),'total':len(results)}
  (REPORT/'package-simulation-results.json').write_text(json.dumps(report,indent=2)+'\n')
