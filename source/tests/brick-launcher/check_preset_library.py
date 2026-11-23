#!/usr/bin/env python3
"""Exercise directory publication, interruptions and no overwrite on real host filesystems."""
from pathlib import Path
import errno,hashlib,importlib.util,os,signal,subprocess,sys,tempfile,time,unittest
from unittest import mock
ROOT=Path(__file__).resolve().parents[2]
SCRIPT=ROOT/'tools/release/prepare_presets.py'
spec=importlib.util.spec_from_file_location('preparer',SCRIPT);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class Library(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='factory library spaces ')
        self.root=Path(self.temp.name);self.factory=self.root/'factory/Presets';self.data=self.root/'data'
        self.sources={'Instruments/Kit/Kick.pti':b'KICK','Instruments/Pad.pti':b'PAD','Delays/Echo.ptd':b'ECHO'}
        h=hashlib.sha256()
        for name,value in sorted(self.sources.items()):
            p=self.factory/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(value)
            h.update(name.encode()+b'\0');h.update(hashlib.sha256(value).digest())
        self.pack=h.hexdigest();(self.factory.parent/'presets-pack.id').write_text(self.pack+'\n')
    def tearDown(self):self.temp.cleanup()
    def verify(self,result):
        for name,value in self.sources.items():
            category,rest=name.split('/',1)
            self.assertEqual((Path(result['libraries'][category])/rest).read_bytes(),value)
        self.assertEqual((self.data/'.factory-presets-pack.id').read_text().strip(),self.pack)
        self.assertFalse(list((self.data/'Presets').rglob('.pte-copy-*')))
    def test_complete_library_keeps_all_personal_presets(self):
        personal=self.data/'Presets/Instruments/Kit/Kick.pti';personal.parent.mkdir(parents=True);personal.write_bytes(b'MY EDIT')
        with mock.patch.object(module,'commit_new_file',side_effect=OSError(errno.EINVAL,'No no-replace rename on this filesystem')):
            result=module.prepare_library(self.factory,self.data)
        self.verify(result);self.assertEqual(personal.read_bytes(),b'MY EDIT')
    def test_personal_factory_folder_collision_is_preserved(self):
        personal=self.data/'Presets/Instruments'/('Factory-'+self.pack[:12])/'Personal.pti'
        personal.parent.mkdir(parents=True);personal.write_text('MY MUSIC')
        result=module.prepare_library(self.factory,self.data);self.verify(result)
        self.assertEqual(personal.read_text(),'MY MUSIC')
        self.assertTrue(result['libraries']['Instruments'].endswith('-2'))
    def test_directory_race_cannot_replace_personal_content(self):
        rename=module.os.rename;made=[]
        def competing_directory(source,target):
            if not made:
                target=Path(target);target.mkdir();(target/'Personal.pti').write_text('MY MUSIC');made.append(target)
            return rename(source,target)
        with mock.patch.object(module.os,'rename',side_effect=competing_directory):
            with self.assertRaises(module.PreparationPaused):module.prepare_library(self.factory,self.data)
        self.assertFalse((self.data/'.factory-presets-pack.id').exists())
        self.assertEqual((made[0]/'Personal.pti').read_text(),'MY MUSIC')
        result=module.prepare_library(self.factory,self.data);self.verify(result)
        self.assertEqual((made[0]/'Personal.pti').read_text(),'MY MUSIC')
    def test_budget_resumes_private_staging_without_partial_visible_library(self):
        clock=[0.0];replace=module.os.replace
        def advance(source,target):
            replace(source,target)
            if Path(source).name.startswith('.pte-copy-'):clock[0]=2.0
        with mock.patch.object(module.time,'monotonic',side_effect=lambda:clock[0]),mock.patch.object(module.os,'replace',side_effect=advance):
            with self.assertRaises(module.PreparationPaused):module.prepare_library(self.factory,self.data,time_budget=1)
        self.assertFalse((self.data/'.factory-presets-pack.id').exists())
        self.assertFalse(list((self.data/'Presets').rglob('*.pti')))
        self.verify(module.prepare_library(self.factory,self.data))
    def test_corrupt_private_stage_is_repaired_without_touching_personal_files(self):
        stage=self.data/'.pte-factory-staging'/self.pack/'Instruments/Kit/Kick.pti';stage.parent.mkdir(parents=True);stage.write_bytes(b'INCOMPLETE')
        (stage.parent/'.pte-copy-leftover').write_bytes(b'PARTIAL')
        personal=self.data/'Presets/Instruments/My.pti';personal.parent.mkdir(parents=True);personal.write_text('USER')
        self.verify(module.prepare_library(self.factory,self.data));self.assertEqual(personal.read_text(),'USER')
        self.assertFalse(list((self.data/'.pte-factory-staging').rglob('.pte-copy-*')))
    @unittest.skipIf(os.name=='nt','SIGKILL fixture uses POSIX process signals')
    def test_real_kill_during_staging_then_resume(self):
        ready=self.root/'ready'
        code="""import importlib.util,sys,time
from pathlib import Path
s=importlib.util.spec_from_file_location('p',sys.argv[1]);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
r=m.os.replace
def slow(source,target):
 r(source,target)
 if Path(source).name.startswith('.pte-copy-'):
  Path(sys.argv[4]).write_text('ready');time.sleep(30)
m.os.replace=slow
m.prepare_library(Path(sys.argv[2]),Path(sys.argv[3]))
"""
        p=subprocess.Popen([sys.executable,'-I','-c',code,str(SCRIPT),str(self.factory),str(self.data),str(ready)],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True)
        try:
            deadline=time.monotonic()+5
            while not ready.exists() and time.monotonic()<deadline and p.poll() is None:time.sleep(.02)
            self.assertTrue(ready.exists())
            with self.assertRaises(module.PreparationPaused):module.prepare_library(self.factory,self.data)
            p.kill();self.assertEqual(p.wait(timeout=3),-signal.SIGKILL)
            self.assertFalse((self.data/'.factory-presets-pack.id').exists())
            self.assertFalse(list((self.data/'Presets').rglob('*.pti')))
            self.verify(module.prepare_library(self.factory,self.data))
        finally:
            if p.poll() is None:p.kill();p.wait()
            p.stderr.close()
    @unittest.skipIf(os.name=='nt','Exercises the device SIGTERM handler')
    def test_real_sigterm_during_staging_cleans_temp_and_resumes(self):
        ready=self.root/'term-ready'
        code="""import importlib.util,sys,time
from pathlib import Path
s=importlib.util.spec_from_file_location('p',sys.argv[1]);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
f=m.os.fsync;ready=Path(sys.argv[4])
def slow(fd):
 ready.write_text('ready');time.sleep(30);f(fd)
m.os.fsync=slow
sys.argv=[sys.argv[1],'--factory',sys.argv[2],'--data',sys.argv[3],'--library','--background']
raise SystemExit(m.main())
"""
        p=subprocess.Popen([sys.executable,'-I','-c',code,str(SCRIPT),str(self.factory),str(self.data),str(ready)],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True)
        try:
            deadline=time.monotonic()+5
            while not ready.exists() and time.monotonic()<deadline and p.poll() is None:time.sleep(.02)
            self.assertTrue(ready.exists())
            p.terminate();self.assertEqual(p.wait(timeout=3),75)
            self.assertFalse((self.data/'.factory-presets-pack.id').exists())
            self.assertFalse(list(self.data.rglob('.pte-copy-*')))
            self.verify(module.prepare_library(self.factory,self.data))
        finally:
            if p.poll() is None:p.kill();p.wait()
            p.stderr.close()
    def test_case_colliding_source_is_rejected_before_writing(self):
        # The host volume may itself fold case. Simulate the directory listing
        # of a case-sensitive source while keeping actual readable source paths.
        paths=[self.factory/'Instruments/Pad.pti',self.factory/'Instruments/pad.PTI']
        if not paths[1].exists():
            paths[1].parent.mkdir(parents=True,exist_ok=True);paths[1].write_bytes(paths[0].read_bytes())
        with mock.patch.object(Path,'rglob',return_value=paths):
            with self.assertRaisesRegex(RuntimeError,'collide'):module.prepare_library(self.factory,self.data)
        self.assertFalse(self.data.exists())
    def test_case_colliding_categories_are_rejected_before_writing(self):
        paths=[self.factory/'Instruments/Pad.pti',self.factory/'instruments/Kit/Kick.pti']
        if not paths[1].exists():
            paths[1].parent.mkdir(parents=True,exist_ok=True);paths[1].write_bytes(paths[0].read_bytes())
        with mock.patch.object(Path,'rglob',return_value=paths):
            with self.assertRaisesRegex(RuntimeError,'category paths collide'):module.prepare_library(self.factory,self.data)
        self.assertFalse(self.data.exists())
    @unittest.skipIf(os.name=='nt','Exercises POSIX kernel lock errors')
    def test_unsupported_lock_is_reported_as_error_not_another_worker(self):
        import fcntl
        with mock.patch.object(fcntl,'flock',side_effect=OSError(errno.ENOTSUP,'controlled unsupported lock')):
            with self.assertRaisesRegex(RuntimeError,'cannot lock'):module.prepare_library(self.factory,self.data)
        self.assertFalse((self.data/'.factory-presets-pack.id').exists())
    def test_resume_preserves_user_edit_in_published_category_before_global_marker(self):
        result=module.prepare_library(self.factory,self.data);self.verify(result)
        edited=Path(result['libraries']['Instruments'])/'Pad.pti';edited.write_text('MY PAD')
        (self.data/'.factory-presets-pack.id').unlink()
        result=module.prepare_library(self.factory,self.data)
        self.assertEqual(edited.read_text(),'MY PAD')
        self.assertEqual((self.data/'.factory-presets-pack.id').read_text().strip(),self.pack)
    def test_factory_corruption_fails_before_data_write(self):
        (self.factory/'Instruments/Pad.pti').write_bytes(b'CORRUPT')
        with self.assertRaises(RuntimeError):module.prepare_library(self.factory,self.data)
        self.assertFalse(self.data.exists())
    def test_category_symlink_is_rejected_without_writing_outside(self):
        outside=self.root/'outside';outside.mkdir();parent=self.data/'Presets';parent.mkdir(parents=True);(parent/'Instruments').symlink_to(outside,target_is_directory=True)
        with self.assertRaises(RuntimeError):module.prepare_library(self.factory,self.data)
        self.assertFalse(list(outside.iterdir()))
if __name__=='__main__':unittest.main(verbosity=2)
