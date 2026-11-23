#!/usr/bin/env python3
"""Exercise the standalone launcher using real Bash and owned process fixtures."""
from pathlib import Path
import hashlib,json,os,shutil,signal,subprocess,tempfile,time,unittest
HERE=Path(__file__).resolve().parent
LAUNCHER=Path(__file__).resolve().parents[2]/'shell/brick/TrackWerk.sh'
NATIVE='''#!/usr/bin/python3
from pathlib import Path
import os,sys,json,time,signal
keys=['SDL_GAMECONTROLLERCONFIG','SDL_GAMECONTROLLERCONFIG_FILE','SDL_VIDEODRIVER','SDL_AUDIODRIVER','SDL_RENDER_DRIVER','LD_LIBRARY_PATH','POCKETTRACKER_HOME','POCKETTRACKER_AUDIO_FRAMES','POCKETTRACKER_FULLSCREEN','POCKETTRACKER_CACERT','POCKETTRACKER_VIDEO','POCKETTRACKER_SYNC_STARTUP','PTE_STARTUP_READY_FILE','PATH']
Path(os.environ['PTE_TEST_SNAPSHOT']).write_text(json.dumps({'cwd':os.getcwd(),'argv':sys.argv[1:],'pid':os.getpid(),'env':{k:os.environ.get(k) for k in keys}}))
mode=os.environ.get('PTE_TEST_MODE','normal')
if mode=='hang': signal.signal(signal.SIGTERM,signal.SIG_IGN)
if mode!='no-frame' and mode!='hang':Path(os.environ['PTE_STARTUP_READY_FILE']).write_text('invalid\\n' if os.environ.get('PTE_TEST_BAD_MARKER') else 'frame submitted\\n')
Path(os.environ['PTE_TEST_READY']).write_text('entered')
if mode in ('wait','no-frame','hang'):
 def stop(sig,frame):
  Path(os.environ['PTE_TEST_SIGNAL']).write_text(str(sig));sys.exit(0)
 if mode!='hang':signal.signal(signal.SIGTERM,stop)
 while True:time.sleep(.02)
if mode=='long-ready':time.sleep(2.3)
if mode=='normal' and Path('factory/Presets').exists():time.sleep(.7)
sys.exit(int(os.environ.get('PTE_TEST_RC','0')))
'''
class Launcher(unittest.TestCase):
 def setUp(self):
  self.root=Path(tempfile.mkdtemp(prefix='card spaces $() ; ` literal ',dir=HERE))
  self.ports=self.root/'ports';self.ports.mkdir();self.app=self.ports/'trackwerk';self.app.mkdir()
  self.script=self.ports/'TrackWerk.sh';shutil.copyfile(LAUNCHER,self.script)
  self.binary=self.app/'trackwerk.aarch64';self.binary.write_text(NATIVE);self.binary.chmod(0o644)
  self.tools=self.app/'tools';self.tools.mkdir();(self.tools/'cacert.pem').write_text('fixture')
  shutil.copyfile(LAUNCHER.parents[2]/'tools/release/prepare_presets.py',self.tools/'prepare_presets.py')
  for name in ['qjs','yt-dlp','pte-iphone-usb','pte-iphone-pair','pte-usbmuxd']:(self.tools/name).write_text('#!/bin/sh\nexit 0\n')
  self.data=self.app/'data';self.data.mkdir();(self.data/'own.ptp').write_text('KEEP')
  self.snapshot=self.root/'snapshot.json';self.log=self.app/'log.txt';self.home=self.root/'home';self.home.mkdir()
  self.env=dict(os.environ)
  for name in ['SDL_GAMECONTROLLERCONFIG','SDL_GAMECONTROLLERCONFIG_FILE','SDL_VIDEODRIVER','SDL_AUDIODRIVER','SDL_RENDER_DRIVER','LD_LIBRARY_PATH','PTE_STARTUP_TIMEOUT_SECONDS','POCKETTRACKER_HOME','POCKETTRACKER_FULLSCREEN','POCKETTRACKER_AUDIO_FRAMES','POCKETTRACKER_CACERT']:
   self.env.pop(name,None)
  self.env.update(HOME=str(self.home),PTE_TEST_SNAPSHOT=str(self.snapshot),PTE_TEST_READY=str(self.root/'ready'),PTE_TEST_SIGNAL=str(self.root/'signal'),PTE_STARTUP_TIMEOUT_SECONDS='3')
  self.proc=None
 def tearDown(self):
  if self.proc and self.proc.poll() is None:
   self.proc.send_signal(signal.SIGTERM)
   try:self.proc.wait(timeout=12)
   except subprocess.TimeoutExpired:self.proc.kill();self.proc.wait()
  shutil.rmtree(self.root)
 def run_app(self,*args,timeout=8):return subprocess.run(['/bin/bash',str(self.script),*args],cwd='/tmp',env=self.env,capture_output=True,text=True,timeout=timeout)
 def snap(self):return json.loads(self.snapshot.read_text())
 def seed(self,entries,pack='a'*64):
  factory=self.app/'factory/Presets'
  for rel,content in entries.items():
   if '/' not in rel:rel='Instruments/'+rel
   target=factory/rel;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(content)
  h=hashlib.sha256()
  for p in sorted(factory.rglob('*'),key=lambda p:p.relative_to(factory).as_posix()):
   if p.is_file() and not any(c.startswith('.') for c in p.relative_to(factory).parts):h.update(p.relative_to(factory).as_posix().encode()+b'\0');h.update(hashlib.sha256(p.read_bytes()).digest())
  self.pack=h.hexdigest();(self.app/'factory/presets-pack.id').write_text(self.pack+'\n')
  return factory
 def launch_wait(self,mode='wait'):
  self.env['PTE_TEST_MODE']=mode
  self.proc=subprocess.Popen(['/bin/bash',str(self.script)],cwd='/tmp',env=self.env)
  deadline=time.monotonic()+5
  while time.monotonic()<deadline and not (self.root/'ready').exists():time.sleep(.02)
  self.assertTrue((self.root/'ready').exists(),self.log.read_text())
 def test_no_portmaster_path_spaces_and_argv(self):
  r=self.run_app('song $literal ; `x`.ptp','media folder');self.assertEqual(r.returncode,0,r.stderr+self.log.read_text())
  s=self.snap();self.assertEqual(s['cwd'],str(self.app));self.assertEqual(s['argv'],['song $literal ; `x`.ptp','media folder'])
  self.assertEqual(s['env']['POCKETTRACKER_HOME'],str(self.data));self.assertEqual((self.data/'own.ptp').read_text(),'KEEP')
  self.assertEqual(s['env']['POCKETTRACKER_VIDEO'],'surface');self.assertEqual(s['env']['POCKETTRACKER_SYNC_STARTUP'],'1');self.assertEqual(s['env']['POCKETTRACKER_AUDIO_FRAMES'],'512');self.assertEqual(s['env']['POCKETTRACKER_FULLSCREEN'],'1')
  self.assertTrue(self.log.read_text().startswith('\n=== TrackWerk 1.3.0-Brick: shell entered'))
  self.assertFalse(list(self.app.glob('.pte-startup-ready.*')))
 def test_first_marker_is_visible_before_external_work(self):
  fake=self.root/'early-tools';fake.mkdir();stub=fake/'wc'
  stub.write_text('#!/bin/sh\nprintf ready > "$PTE_TEST_WC_READY"\nwhile [ ! -e "$PTE_TEST_WC_RELEASE" ]; do sleep 0.05; done\nexec /usr/bin/wc "$@"\n');stub.chmod(0o755)
  release=self.root/'wc-release';ready=self.root/'wc-ready'
  self.env.update(PATH=str(fake)+os.pathsep+self.env['PATH'],PTE_TEST_WC_READY=str(ready),PTE_TEST_WC_RELEASE=str(release))
  self.proc=subprocess.Popen(['/bin/bash',str(self.script)],cwd='/tmp',env=self.env)
  try:
   deadline=time.monotonic()+5
   while not ready.exists() and time.monotonic()<deadline:time.sleep(.02)
   self.assertTrue(ready.exists());self.assertIsNone(self.proc.poll());self.assertFalse(self.snapshot.exists())
   self.assertIn('shell entered',self.log.read_text())
  finally:release.write_text('continue')
  self.assertEqual(self.proc.wait(timeout=5),0)
 def test_log_precedes_sync_and_native_launch(self):
  fake=self.root/'sync-tools';fake.mkdir();stub=fake/'sync'
  stub.write_text('#!/bin/sh\nif [ ! -e "$PTE_SYNC_CAPTURE" ]; then cp "$PTE_SYNC_LOG" "$PTE_SYNC_CAPTURE"; test ! -e "$PTE_TEST_SNAPSHOT" || exit 91; fi\n');stub.chmod(0o755)
  capture=self.root/'initial-sync-log'
  self.env.update(PATH=str(fake)+os.pathsep+self.env['PATH'],PTE_SYNC_LOG=str(self.log),PTE_SYNC_CAPTURE=str(capture))
  self.assertEqual(self.run_app().returncode,0)
  self.assertIn('shell entered',capture.read_text());self.assertNotIn('Launching native',capture.read_text())
 def test_explicit_video_mode_preserved(self):
  self.env['POCKETTRACKER_VIDEO']='renderer'
  self.assertEqual(self.run_app().returncode,0)
  self.assertEqual(self.snap()['env']['POCKETTRACKER_VIDEO'],'renderer')
 def test_hostile_pm_helpers_are_never_sourced(self):
  pm=self.ports/'PortMaster';pm.mkdir();(pm/'control.txt').write_text('echo SHOULD_NOT_RUN; exit 88\n')
  self.env['POCKETTRACKER_PORTMASTER']=str(pm)
  r=self.run_app();self.assertEqual(r.returncode,0);self.assertNotIn('SHOULD_NOT_RUN',self.log.read_text())
 def test_explicit_maps_and_driver_choices_preserved(self):
  self.env.update(SDL_GAMECONTROLLERCONFIG='MYMAP\nSECOND',SDL_GAMECONTROLLERCONFIG_FILE='/custom missing db',SDL_VIDEODRIVER='user-video',SDL_AUDIODRIVER='user-audio',SDL_RENDER_DRIVER='user-render',LD_LIBRARY_PATH='/user libraries',POCKETTRACKER_AUDIO_FRAMES='1024',POCKETTRACKER_FULLSCREEN='0',POCKETTRACKER_CACERT='/user/ca.pem')
  self.assertEqual(self.run_app().returncode,0)
  for key in ['SDL_GAMECONTROLLERCONFIG','SDL_GAMECONTROLLERCONFIG_FILE','SDL_VIDEODRIVER','SDL_AUDIODRIVER','SDL_RENDER_DRIVER','LD_LIBRARY_PATH','POCKETTRACKER_AUDIO_FRAMES','POCKETTRACKER_FULLSCREEN','POCKETTRACKER_CACERT']:self.assertEqual(self.snap()['env'][key],self.env[key])
 def test_explicit_empty_mapping_file_is_preserved(self):
  self.env['SDL_GAMECONTROLLERCONFIG_FILE']='';self.assertEqual(self.run_app().returncode,0);self.assertEqual(self.snap()['env']['SDL_GAMECONTROLLERCONFIG_FILE'],'')
 def test_mapping_file_fallback(self):
  folder=self.home/'.config';folder.mkdir();db=folder/'gamecontrollerdb.txt';db.write_text('userdb')
  self.assertEqual(self.run_app().returncode,0)
  # A real KNULLI /tmp database has precedence if this host has one.
  expected='/tmp/gamecontrollerdb.txt' if Path('/tmp/gamecontrollerdb.txt').is_file() else str(db)
  self.assertEqual(self.snap()['env']['SDL_GAMECONTROLLERCONFIG_FILE'],expected)
 def test_native_failure_code_preserved(self):
  self.env['PTE_TEST_RC']='17';self.assertEqual(self.run_app().returncode,17);self.assertIn('Status 17',self.log.read_text())
 def test_missing_native_logs_exits_without_gui(self):
  self.binary.unlink();self.assertEqual(self.run_app().returncode,2);self.assertIn('App-Datei',self.log.read_text());self.assertFalse(self.snapshot.exists())
 def test_seed_nested_names_and_ignore_mac_metadata(self):
  self.seed({'Instruments/Drum Lab/Kick 01.pti':'KICK','._ignore.pti':'METADATA','.hidden/ignore.pti':'METADATA','Instruments/.hidden.pti':'METADATA'})
  self.assertEqual(self.run_app().returncode,0,self.log.read_text())
  self.assertEqual(next((self.data/'Presets/Instruments').glob('Factory-*/Drum Lab/Kick 01.pti')).read_text(),'KICK')
  self.assertFalse((self.data/'Presets/._ignore.pti').exists());self.assertFalse((self.data/'Presets/.hidden').exists());self.assertFalse((self.data/'Presets/Instruments/.hidden.pti').exists())
  self.assertEqual((self.data/'.factory-presets-pack.id').read_text(),self.pack+'\n')
 def test_same_pack_skips_find_and_copy(self):
  self.seed({'A.pti':'A'});self.assertEqual(self.run_app().returncode,0)
  fake=self.root/'fake-tools';fake.mkdir()
  for cmd in ['find','cp','cmp']:
   f=fake/cmd;f.write_text('#!/bin/sh\necho unexpected '+cmd+' >&2\nexit 93\n');f.chmod(0o755)
  self.env['PATH']=str(fake)+os.pathsep+self.env['PATH']
  self.assertEqual(self.run_app().returncode,0,self.log.read_text());self.assertIn('matching pack identifier',self.log.read_text())
 def test_changed_pack_adds_presets_preserves_edits(self):
  self.seed({'A.pti':'A'});self.assertEqual(self.run_app().returncode,0)
  next((self.data/'Presets/Instruments').glob('Factory-*/A.pti')).write_text('MY EDIT')
  self.seed({'A.pti':'NEW FACTORY A','B.pti':'B'},'b'*64);self.assertEqual(self.run_app().returncode,0)
  self.assertTrue(any(p.read_text()=='MY EDIT' for p in (self.data/'Presets/Instruments').glob('Factory-*/A.pti')));self.assertEqual(next((self.data/'Presets/Instruments').glob('Factory-*/B.pti')).read_text(),'B');self.assertEqual((self.data/'.factory-presets-pack.id').read_text(),self.pack+'\n')
 def test_existing_symlink_preset_is_not_replaced(self):
  self.seed({'A.pti':'A'});(self.data/'Presets/Instruments').mkdir(parents=True);outside=self.root/'own.pti';outside.write_text('OWN');(self.data/'Presets/Instruments/A.pti').symlink_to(outside)
  self.assertEqual(self.run_app().returncode,0);self.assertTrue((self.data/'Presets/Instruments/A.pti').is_symlink());self.assertEqual(outside.read_text(),'OWN')
 def test_seed_refuses_symlink_parent(self):
  self.seed({'Category/A.pti':'A'});(self.data/'Presets').mkdir();outside=self.root/'outside';outside.mkdir();(self.data/'Presets/Category').symlink_to(outside,target_is_directory=True)
  self.assertEqual(self.run_app().returncode,0);self.assertFalse((outside/'A.pti').exists());self.assertFalse((self.data/'.factory-presets-pack.id').exists());self.assertIn('Preparation stopped',self.log.read_text())
 def test_bad_factory_does_not_block_app_or_commit_marker(self):
  self.seed({'A.pti':'A'});(self.app/'factory/Presets/Instruments/A.pti').write_text('CORRUPT')
  self.assertEqual(self.run_app().returncode,0)
  self.assertFalse((self.data/'.factory-presets-pack.id').exists());self.assertFalse((self.data/'Presets/A.pti').exists());self.assertFalse(list(self.data.rglob('.pte-*')))
  self.assertIn('Factory pack verification failed',self.log.read_text());self.assertIn('first frame submitted',self.log.read_text())
 def test_preset_worker_only_starts_after_native_first_frame(self):
  self.seed({'A.pti':'A'});self.assertEqual(self.run_app().returncode,0)
  log=self.log.read_text();self.assertLess(log.index('Native first frame submitted'),log.index('Preparing factory library in background'))
  self.assertLess(log.index('Native first frame submitted'),log.index('Factory pack verified'))
 def test_no_preset_worker_if_native_has_no_frame(self):
  self.seed({'A.pti':'A'});self.env.update(PTE_TEST_MODE='no-frame',PTE_STARTUP_TIMEOUT_SECONDS='1')
  self.assertEqual(self.run_app(timeout=6).returncode,124)
  self.assertFalse((self.data/'Presets/A.pti').exists());self.assertNotIn('Preparing factory library in background',self.log.read_text())
 def test_hung_preset_worker_never_delays_first_frame_and_is_cancelled(self):
  self.seed({'A.pti':'A'})
  (self.tools/'prepare_presets.py').write_text("import os,signal,time\nfrom pathlib import Path\nsignal.signal(signal.SIGTERM,signal.SIG_IGN)\nPath(os.environ['PTE_TEST_WORKER_READY']).write_text(str(os.getpid()))\nwhile True:time.sleep(.03)\n")
  ready=self.root/'worker-ready';self.env['PTE_TEST_WORKER_READY']=str(ready)
  self.env['SHELLOPTS']='errexit';self.launch_wait();deadline=time.monotonic()+4
  while not ready.exists() and time.monotonic()<deadline:time.sleep(.02)
  self.assertTrue(ready.exists(),self.log.read_text());self.assertIn('Native first frame submitted',self.log.read_text());self.assertIsNone(self.proc.poll())
  pid=int(ready.read_text());os.kill(pid,0);started=time.monotonic();self.proc.send_signal(signal.SIGTERM)
  time.sleep(.2);self.proc.send_signal(signal.SIGTERM)
  self.assertEqual(self.proc.wait(timeout=6),143);self.assertLess(time.monotonic()-started,4);self.assertRegex(self.log.read_text(),r'Shell flags: \S*e')
  with self.assertRaises(ProcessLookupError):os.kill(pid,0)
  self.assertFalse((self.data/'.factory-presets-pack.id').exists())
 def test_matching_marker_respects_deleted_and_renamed_user_presets(self):
  self.seed({'A.pti':'A','B.pti':'B'});self.assertEqual(self.run_app().returncode,0)
  folder=next((self.data/'Presets/Instruments').glob('Factory-*'))
  (folder/'A.pti').rename(folder/'My A.pti');(folder/'B.pti').unlink()
  self.assertEqual(self.run_app().returncode,0)
  self.assertEqual((folder/'My A.pti').read_text(),'A');self.assertFalse((folder/'A.pti').exists());self.assertFalse((folder/'B.pti').exists())
  self.assertIn('matching pack identifier',self.log.read_text())
 def test_missing_system_python_does_not_prevent_native_start(self):
  self.seed({'A.pti':'A'});fake=self.root/'no-python';fake.mkdir()
  for command in ['sync','wc','mv','mkdir','chmod','sleep','rm']:
   (fake/command).symlink_to(shutil.which(command))
  self.env['PATH']=str(fake)
  self.assertEqual(self.run_app().returncode,0);self.assertIn('system python3 is missing',self.log.read_text());self.assertIn('Native first frame submitted',self.log.read_text())
 def test_preset_worker_isolated_from_python_shadow_modules(self):
  self.seed({'A.pti':'A'});bad=self.root/'untrusted-python';bad.mkdir()
  sentinel=self.root/'shadow-imported'
  payload='from pathlib import Path\nPath('+repr(str(sentinel))+').write_text("BAD")\nraise RuntimeError("shadowed")\n'
  (bad/'hashlib.py').write_text(payload);(self.tools/'hashlib.py').write_text(payload)
  self.env['PYTHONPATH']=str(bad)
  self.assertEqual(self.run_app().returncode,0);self.assertFalse(sentinel.exists());self.assertTrue((self.data/'.factory-presets-pack.id').exists())
 def test_worker_error_does_not_replace_native_exit_status(self):
  self.seed({'A.pti':'A'});(self.app/'factory/Presets/Instruments/A.pti').write_text('BAD');self.env['PTE_TEST_RC']='17'
  self.assertEqual(self.run_app().returncode,17);self.assertIn('Factory pack verification failed',self.log.read_text());self.assertIn('Status 17',self.log.read_text())
 def test_errexit_worker_pause_preserves_native_success(self):
  self.seed({'A.pti':'A'});(self.tools/'prepare_presets.py').write_text('raise SystemExit(75)\n');self.env['SHELLOPTS']='errexit'
  r=self.run_app();self.assertEqual(r.returncode,0,self.log.read_text());self.assertIn('Background preset preparation exited with status 75',self.log.read_text());self.assertIn('Status 0',self.log.read_text());self.assertRegex(self.log.read_text(),r'Shell flags: \S*e')
 def test_errexit_native_failure_preserves_status_and_cleanup(self):
  self.env.update(SHELLOPTS='errexit',PTE_TEST_RC='17')
  r=self.run_app();self.assertEqual(r.returncode,17,self.log.read_text());self.assertIn('Status 17',self.log.read_text());self.assertRegex(self.log.read_text(),r'Shell flags: \S*e')
 def test_worker_keeps_running_without_session_copy_budget(self):
  self.seed({'A.pti':'A'});args=self.root/'worker-args';self.env['PTE_TEST_WORKER_ARGS']=str(args)
  (self.tools/'prepare_presets.py').write_text("import os,sys,json\nfrom pathlib import Path\nPath(os.environ['PTE_TEST_WORKER_ARGS']).write_text(json.dumps(sys.argv[1:]))\n")
  self.assertEqual(self.run_app().returncode,0)
  values=json.loads(args.read_text());self.assertIn('--background',values);self.assertIn('--library',values);self.assertNotIn('--time-budget',values)
 def test_errexit_startup_timeout_and_native_kill(self):
  for mode in ['no-frame','hang']:
   self.env.update(SHELLOPTS='errexit',PTE_TEST_MODE=mode,PTE_STARTUP_TIMEOUT_SECONDS='1')
   self.assertEqual(self.run_app(timeout=14).returncode,124,self.log.read_text());self.assertRegex(self.log.read_text(),r'Shell flags: \S*e')
 def test_real_first_frame_disarms_watchdog(self):
  self.env.update(PTE_TEST_MODE='long-ready',PTE_STARTUP_TIMEOUT_SECONDS='1');self.assertEqual(self.run_app().returncode,0,self.log.read_text());self.assertIn('first frame submitted',self.log.read_text())
 def test_invalid_marker_does_not_disable_watchdog(self):
  self.env.update(PTE_TEST_MODE='long-ready',PTE_TEST_BAD_MARKER='1',PTE_STARTUP_TIMEOUT_SECONDS='1')
  self.assertEqual(self.run_app(timeout=6).returncode,124,self.log.read_text())
  self.assertNotIn('startup watchdog complete',self.log.read_text())
 def test_first_frame_timeout_returns_frontend(self):
  self.env.update(PTE_TEST_MODE='no-frame',PTE_STARTUP_TIMEOUT_SECONDS='1');started=time.monotonic();r=self.run_app(timeout=6)
  self.assertEqual(r.returncode,124,self.log.read_text());self.assertLess(time.monotonic()-started,4);self.assertIn('STARTUP TIMEOUT',self.log.read_text());self.assertTrue((self.root/'signal').exists())
 def test_hung_child_is_killed_within_bound(self):
  self.env.update(PTE_TEST_MODE='hang',PTE_STARTUP_TIMEOUT_SECONDS='1');started=time.monotonic();r=self.run_app(timeout=13)
  self.assertEqual(r.returncode,124,self.log.read_text());self.assertLess(time.monotonic()-started,11);self.assertIn('status 137',self.log.read_text())
  with self.assertRaises(ProcessLookupError):os.kill(self.snap()['pid'],0)
 def test_termination_saves_and_preserves_signal_status(self):
  self.launch_wait();self.proc.send_signal(signal.SIGTERM);self.assertEqual(self.proc.wait(timeout=5),143);self.assertEqual((self.root/'signal').read_text(),str(int(signal.SIGTERM)));self.assertIn('Native process stopped with status 0',self.log.read_text())
 def test_invalid_startup_deadline(self):
  for value in ['0','121','-1','bad','1000','1;echo bad']:
   self.env['PTE_STARTUP_TIMEOUT_SECONDS']=value;self.assertEqual(self.run_app().returncode,2,value)
  self.env['PTE_STARTUP_TIMEOUT_SECONDS']='001';self.assertEqual(self.run_app().returncode,0)
 def test_rotation_and_helpers(self):
  previous='OLD\n'+'x'*(1048576+50);self.log.write_text(previous);self.assertEqual(self.run_app().returncode,0)
  self.assertTrue((self.app/'log.previous.txt').read_text().startswith(previous));self.assertNotIn('OLD\n',self.log.read_text())
  for n in ['qjs','yt-dlp','pte-iphone-usb','pte-iphone-pair','pte-usbmuxd']:self.assertTrue(os.access(self.tools/n,os.X_OK))

if __name__=='__main__':
 suite=unittest.defaultTestLoader.loadTestsFromTestCase(Launcher)
 result=unittest.TextTestRunner(verbosity=2).run(suite)
 (HERE/'result.json').write_text(json.dumps({'passed':result.wasSuccessful(),'tests':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),'method':'Actual standalone Bash launcher and owned native process fixtures; no device/USB/network access'},indent=2)+'\n')
 raise SystemExit(0 if result.wasSuccessful() else 1)
