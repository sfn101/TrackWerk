#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Add verified bundled samples/manuals to an existing data home; never overwrite existing files."""
import argparse,hashlib,json,os,shutil
from pathlib import Path,PurePosixPath

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def checked(root,relative=''):
    root=Path(os.path.abspath(root));rel=PurePosixPath(relative)
    if rel.is_absolute() or '..' in rel.parts or '\\' in relative:raise ValueError('Unsafe relative path')
    p=root.joinpath(*rel.parts)
    if any(x.is_symlink() for x in [p,*p.parents]):raise ValueError('Symbolic-link path refused')
    return p

def install(home,bundle,apply=False):
    home=checked(home);bundle=checked(bundle)
    if not home.is_dir():raise ValueError('Choose your existing TrackWerk data folder')
    rows=json.loads((bundle/'CONTENT-MANIFEST.json').read_text())['files'];add=[]
    for row in rows:
        rel=PurePosixPath(row['path'])
        if not rel.parts or rel.parts[0] not in ['Samples','Handbuch-Manual']:raise ValueError('Unexpected shared-data target')
        source=checked(bundle,row['source']);target=checked(home,row['path'])
        if not source.is_file() or source.stat().st_size!=row['bytes'] or sha(source)!=row['sha256']:raise ValueError('Bundled-file checksum failed')
        if target.exists():
            if not target.is_file():raise ValueError('Existing target is not a regular file')
        else:add.append((row,source,target))
    result={'version':'1.3.0','plan_only':not apply,'files':len(rows),'missing_files':len(add),'existing_files_retained':len(rows)-len(add),'overwrites_existing_files':False}
    if not apply:return result
    if shutil.disk_usage(home).free<sum(row['bytes'] for row,_,_ in add)+16*1024*1024:raise ValueError('Not enough free space with reserve')
    added=0
    for row,source,target in add:
        checked(home,row['path']);target.parent.mkdir(parents=True,exist_ok=True)
        try:fd=os.open(target,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o644)
        except FileExistsError:
            if not target.is_file() or target.is_symlink():raise ValueError('Destination changed')
            continue
        owned=os.fstat(fd)
        try:
            with os.fdopen(fd,'wb') as out,source.open('rb') as inp:shutil.copyfileobj(inp,out);out.flush();os.fsync(out.fileno())
            if sha(target)!=row['sha256']:raise ValueError('Copied-file checksum failed')
        except BaseException:
            current=target.lstat()
            if (current.st_dev,current.st_ino)==(owned.st_dev,owned.st_ino):target.unlink()
            raise
        added+=1
    result['added_files']=added;return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--data-home',type=Path,required=True);p.add_argument('--apply',action='store_true');a=p.parse_args()
    print(json.dumps(install(a.data_home,Path(__file__).resolve().parent,a.apply),indent=2))
if __name__=='__main__':main()
