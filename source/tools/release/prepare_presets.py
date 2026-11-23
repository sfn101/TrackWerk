#!/usr/bin/env python3
"""Optional computer-side first-run preparation. Python3 only; never overwrite an existing preset."""
from pathlib import Path
import argparse,errno,hashlib,os,signal,sys,tempfile,time,unicodedata
try:
    import ctypes
except ImportError:
    ctypes = None  # Directory publication does not require this optional module.


class PreparationPaused(RuntimeError):
    """An incomplete setup is safe to retry; never publish a new pack marker."""


def flush_directory(path):
    if os.name == 'nt':
        return  # Windows users must safely eject; file contents are flushed below.
    fd = os.open(path, os.O_RDONLY)
    try:
        try:
            os.fsync(fd)
        except OSError as error:
            if error.errno not in (errno.EINVAL, errno.ENOTSUP, errno.EOPNOTSUPP):
                raise
    finally:
        os.close(fd)


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


def commit_new_file(temp, target):
    """Publish without replacement; stop if the mounted filesystem cannot do it."""
    try:
        _commit_new_file(temp, target)
    except OSError as error:
        if error.errno in (errno.ENOTSUP, errno.EOPNOTSUPP, errno.ENOSYS):
            raise RuntimeError(
                'This mounted filesystem does not support the atomic no-replace '
                'operation required for computer preparation. Existing presets '
                'and the pack marker have been preserved. Safely eject the card '
                'and start TrackWerk on the device; its first launch '
                'will prepare missing presets.'
            ) from error
        raise


def _commit_new_file(temp, target):
    """Use only operations that atomically refuse to replace an existing file."""
    if os.name=='nt':
        os.rename(temp,target)  # Windows rename fails if the destination exists.
        return
    if ctypes is None:
        raise RuntimeError('Atomic per-file preparation requires ctypes; use --library instead.')
    libc=ctypes.CDLL(None,use_errno=True)
    if sys.platform=='darwin' and hasattr(libc,'renamex_np'):
        call=libc.renamex_np
        call.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_uint]
        result=call(os.fsencode(temp),os.fsencode(target),4)  # RENAME_EXCL
    elif hasattr(libc,'renameat2'):
        call=libc.renameat2
        call.argtypes=[ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint]
        result=call(-100,os.fsencode(temp),-100,os.fsencode(target),1)  # RENAME_NOREPLACE
    else:
        # Atomic no-clobber fallback for filesystems with hard-link support.
        os.link(temp,target)
        temp.unlink()
        return
    if result:
        error=ctypes.get_errno()
        raise OSError(error,os.strerror(error),str(target))


def check_parents(root, relative):
    current=root
    if current.is_symlink():raise RuntimeError('Data/preset root is a symbolic link; preparation stopped.')
    for part in relative.parts[:-1]:
        current=current/part
        if current.is_symlink():raise RuntimeError('Preset subfolder is a symbolic link; preparation stopped.')
        if current.exists() and not current.is_dir():raise RuntimeError('A preset parent is not a directory.')


def prepare(factory, data_dir, time_budget=None):
    deadline = time.monotonic() + time_budget if time_budget is not None else None
    def check_budget():
        if deadline is not None and time.monotonic() >= deadline:
            raise PreparationPaused('Time budget reached; missing presets resume next launch.')
    factory=Path(factory)
    data_dir=Path(data_dir)
    if factory.is_symlink() or not factory.is_dir():raise RuntimeError('Factory preset directory is missing or linked.')
    expected=(factory.parent/'presets-pack.id').read_text().strip()
    if len(expected)!=64:raise RuntimeError('Factory pack identifier is invalid.')
    entries=[];pack=hashlib.sha256()
    # Verify all sources before touching the destination.
    for path in sorted(factory.rglob('*'),key=lambda p:p.relative_to(factory).as_posix()):
        check_budget()
        if path.is_symlink():raise RuntimeError('Linked factory content is not supported.')
        if not path.is_file() or any(part.startswith('.') for part in path.relative_to(factory).parts):continue
        relative=path.relative_to(factory);value=digest(path)
        pack.update(relative.as_posix().encode()+b'\0');pack.update(value)
        entries.append((path,relative,value))
    if pack.hexdigest()!=expected:raise RuntimeError('Factory pack verification failed; no files were copied.')
    if data_dir.is_symlink():raise RuntimeError('Data directory must not be a symbolic link.')
    destination=data_dir/'Presets'
    for _,relative,_ in entries:check_parents(destination,relative)
    destination.mkdir(parents=True,exist_ok=True)
    added=0;preserved=0
    touched = {data_dir, destination}
    print('Factory pack verified:', len(entries), 'presets; preparing missing files', flush=True)
    for source,relative,expected_digest in entries:
        check_budget()
        target=destination/relative
        check_parents(destination,relative)
        if target.is_symlink() or target.exists():
            if target.is_dir():raise RuntimeError('A preset target is a directory.')
            preserved+=1
            continue
        target.parent.mkdir(parents=True,exist_ok=True)
        parent = target.parent
        while parent != data_dir:
            touched.add(parent)
            parent = parent.parent
        fd,name=tempfile.mkstemp(prefix='.pte-preset-',dir=target.parent)
        temp=Path(name)
        try:
            with os.fdopen(fd,'wb') as output:
                output.write(source.read_bytes())
                output.flush()
                os.fsync(output.fileno())
            if digest(temp)!=expected_digest:raise RuntimeError('A copied preset failed verification.')
            check_budget()
            try:commit_new_file(temp,target)
            except FileExistsError:
                preserved+=1
                continue
            added+=1
        finally:
            if temp.exists():temp.unlink()
        if added == 1 or added%128==0:print('Prepared',added,'missing presets',flush=True)
    # Flush just this setup's directories, not every project's writes on the
    # device. Each new file was flushed before publication above.
    for directory in sorted(touched, key=lambda p: len(p.parts), reverse=True):
        check_budget()
        flush_directory(directory)
    check_budget()
    marker=data_dir/'.factory-presets-pack.id'
    fd,name=tempfile.mkstemp(prefix='.pte-pack-',dir=data_dir)
    temp=Path(name)
    try:
        with os.fdopen(fd,'w') as output:
            output.write(expected+'\n');output.flush();os.fsync(output.fileno())
        os.replace(temp,marker)
        flush_directory(data_dir)
    finally:
        if temp.exists():temp.unlink()
    return {'added':added,'preserved':preserved,'factory_files':len(entries),'pack_id':expected}


def prepare_library(factory, data_dir, time_budget=None):
    """Publish complete category folders; no per-file no-replace support required.

    Only our hidden staging tree is repairable. User-visible files are never
    merged or replaced. Rename cannot replace a non-empty destination directory,
    including on FAT/exFAT implementations without renameat2(NOREPLACE).
    """
    factory, data_dir = Path(factory), Path(data_dir)
    verification_start = time.monotonic()
    deadline = None  # Verification is background work; budget the resumable copying separately.
    def check_budget():
        if deadline is not None and time.monotonic() >= deadline:
            raise PreparationPaused('Time budget reached; library staging resumes next launch.')
    if factory.is_symlink() or not factory.is_dir():
        raise RuntimeError('Factory preset directory is missing or linked.')
    expected = (factory.parent/'presets-pack.id').read_text().strip()
    print('Verifying factory pack for library setup',flush=True)
    entries = []; pack = hashlib.sha256(); names = set(); category_names = {}
    for path in sorted(factory.rglob('*'), key=lambda p:p.relative_to(factory).as_posix()):
        check_budget()
        if path.is_symlink(): raise RuntimeError('Linked factory content is not supported.')
        relative = path.relative_to(factory)
        if not path.is_file() or any(part.startswith('.') for part in relative.parts): continue
        if len(relative.parts) < 2: raise RuntimeError('Factory presets must be inside a category.')
        category = relative.parts[0]
        category_key = unicodedata.normalize('NFC',category).casefold()
        if category_names.setdefault(category_key,category) != category:
            raise RuntimeError('Factory category paths collide on FAT/exFAT.')
        key = unicodedata.normalize('NFC',relative.as_posix()).casefold()
        if key in names: raise RuntimeError('Factory preset paths collide on FAT/exFAT.')
        names.add(key)
        value = digest(path)
        pack.update(relative.as_posix().encode()+b'\0'); pack.update(value)
        entries.append((path,relative,value))
    if not entries or pack.hexdigest() != expected:
        raise RuntimeError('Factory pack verification failed; no files were copied.')
    elapsed = time.monotonic() - verification_start
    print('Factory verification complete in %.2f seconds' % elapsed,flush=True)
    deadline = time.monotonic() + time_budget if time_budget is not None else None
    if data_dir.is_symlink(): raise RuntimeError('Data directory must not be a symbolic link.')
    data_dir.mkdir(parents=True, exist_ok=True)
    destination = data_dir/'Presets'
    categories = sorted({relative.parts[0] for _,relative,_ in entries})
    for category in categories: check_parents(destination, Path(category)/'placeholder')
    destination.mkdir(exist_ok=True)
    stage_root = data_dir/'.pte-factory-staging'
    if stage_root.is_symlink(): raise RuntimeError('Factory staging directory is linked.')
    stage_root.mkdir(exist_ok=True)
    lock_path = stage_root/'lock'
    if lock_path.is_symlink(): raise RuntimeError('Factory staging lock is linked.')
    # Kernel locks are released even on SIGKILL. A second worker never edits an
    # active transaction. The app itself never writes inside this hidden tree.
    with lock_path.open('a+b') as lock:
        try:
            if os.name == 'nt':
                import msvcrt
                if lock.tell() == 0: lock.write(b'\0'); lock.flush()
                lock.seek(0); msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            if error.errno in (errno.EAGAIN,errno.EWOULDBLOCK,errno.EACCES,errno.EDEADLK):
                raise PreparationPaused('Another library setup is already running.') from error
            raise RuntimeError('This filesystem cannot lock the private library staging area: '+str(error)) from error
        stage = stage_root/expected
        if stage.is_symlink(): raise RuntimeError('Factory staging pack is linked.')
        stage.mkdir(exist_ok=True)
        # Only our private, locked transaction tree is cleaned or repaired.
        # A SIGKILL may leave a temporary copy; no personal preset lives here.
        for leftover in stage.rglob('.pte-copy-*'):
            if leftover.is_file() or leftover.is_symlink(): leftover.unlink()
        added = 0; published = {}; tag = '.trackwerk-factory-pack'
        print('Factory pack verified:',len(entries),'presets; staging complete category folders',flush=True)
        for category in categories:
            check_budget()
            parent = destination/category
            check_parents(destination, Path(category)/'placeholder')
            parent.mkdir(exist_ok=True)
            # Respect an earlier completed publication, including user edits and
            # pruning. A collision with a personal folder gets a different name.
            base = 'Factory-'+expected[:12]
            target = None
            for number in range(1000):
                candidate = parent/(base if number == 0 else base+'-'+str(number+1))
                if candidate.is_symlink(): continue
                if candidate.is_dir() and (candidate/tag).is_file():
                    try: owned = (candidate/tag).read_text().strip() == expected
                    except (OSError,ValueError): owned = False
                    if owned:
                        target = candidate; break
                if not candidate.exists(): target = candidate; break
            if target is None: raise RuntimeError('No unused factory library folder name remains.')
            if target.exists():
                published[category] = str(target)
                continue
            staged = stage/category
            if staged.is_symlink(): raise RuntimeError('Staged category is linked.')
            staged.mkdir(exist_ok=True)
            touched = {staged}
            for source,relative,value in entries:
                if relative.parts[0] != category: continue
                check_budget()
                inside = Path(*relative.parts[1:]); check_parents(staged,inside)
                copied = staged/inside
                if copied.is_symlink(): raise RuntimeError('Staged preset is linked.')
                if copied.is_file() and digest(copied) == value: continue
                copied.parent.mkdir(parents=True, exist_ok=True)
                current = copied.parent
                while current != stage:
                    touched.add(current); current = current.parent
                fd,name = tempfile.mkstemp(prefix='.pte-copy-',dir=copied.parent)
                temp = Path(name)
                try:
                    with os.fdopen(fd,'wb') as output:
                        output.write(source.read_bytes()); output.flush(); os.fsync(output.fileno())
                    if digest(temp) != value: raise RuntimeError('Staged preset verification failed.')
                    check_budget()
                    os.replace(temp,copied)  # Only inside this worker's private locked staging tree.
                finally:
                    if temp.exists(): temp.unlink()
                added += 1
                if added == 1 or added%128 == 0: print('Staged',added,'missing presets',flush=True)
            # No partially written preset becomes visible in the browser.
            with (staged/tag).open('w') as marker:
                marker.write(expected+'\n'); marker.flush(); os.fsync(marker.fileno())
            # Include directories from earlier interrupted runs as well.
            for path in staged.rglob('*'):
                if path.is_symlink(): raise RuntimeError('Staged category contains a link.')
                if path.is_dir(): touched.add(path)
            for directory in sorted(touched,key=lambda p:len(p.parts),reverse=True):
                check_budget(); flush_directory(directory)
            check_budget()
            try:
                os.rename(staged,target)
            except OSError as error:
                if error.errno in (errno.EEXIST,errno.ENOTEMPTY):
                    raise PreparationPaused('A library folder appeared during setup; retrying next launch.') from error
                raise
            flush_directory(parent); flush_directory(stage)
            published[category] = str(target)
            print('Library ready:',target,flush=True)
        check_budget()
        for target in published.values():
            path = Path(target)
            if path.is_symlink() or not path.is_dir() or (path/tag).read_text().strip() != expected:
                raise PreparationPaused('A completed category changed during setup; retrying next launch.')
        # Persist category entries and the Presets entry itself before the global
        # marker can tell future launches that installation is complete.
        for directory in (destination,stage,stage_root,data_dir):
            check_budget(); flush_directory(directory)
        marker = data_dir/'.factory-presets-pack.id'
        fd,name = tempfile.mkstemp(prefix='.pte-pack-',dir=data_dir); temp=Path(name)
        try:
            with os.fdopen(fd,'w') as output:
                output.write(expected+'\n'); output.flush(); os.fsync(output.fileno())
            os.replace(temp,marker); flush_directory(data_dir)
        finally:
            if temp.exists(): temp.unlink()
        return {'added':added,'preserved':0,'factory_files':len(entries),'pack_id':expected,'libraries':published}


def main():
    here=Path(__file__).resolve()
    candidates=[here.parents[1]/'factory/Presets',here.parents[2]/'factory/Presets']
    default=next((p for p in candidates if p.is_dir()),None)
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--factory',type=Path,default=default)
    parser.add_argument('--data',type=Path)
    parser.add_argument('--background',action='store_true',help='Device worker: lower CPU priority and stop safely when the app closes.')
    parser.add_argument('--library',action='store_true',help='Install complete Factory subfolders without overwriting personal presets; works on FAT/exFAT.')
    parser.add_argument('--time-budget',type=float,help='Maximum setup work per launch in seconds; missing files resume next time.')
    args=parser.parse_args()
    if args.time_budget is not None and not 0 < args.time_budget <= 600:
        parser.error('--time-budget must be greater than zero and at most 600 seconds.')
    if args.factory is None:parser.error('Use --factory to select the verified factory/Presets folder.')
    data_dir=args.data or args.factory.parent.parent/'data'
    if args.background:
        def pause(signum, frame):
            raise PreparationPaused('App closed; missing presets resume next launch.')
        signal.signal(signal.SIGTERM, pause)
        signal.signal(signal.SIGINT, pause)
        if hasattr(os, 'nice'):
            try: os.nice(10)
            except OSError: pass
    try:
        result=(prepare_library if args.library else prepare)(args.factory,data_dir,args.time_budget)
        print('Ready. New files:',result['added'],'Existing files kept:',result['preserved'])
        print('Data folder:',data_dir)
    except PreparationPaused as error:
        print('Preparation paused:',error,flush=True)
        return 75
    except (OSError,RuntimeError,ValueError) as error:
        print('Preparation stopped:',error,file=sys.stderr)
        return 1
    return 0


if __name__=='__main__':raise SystemExit(main())
