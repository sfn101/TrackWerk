#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""TrackWerk 1.3 labels, menu art and shortcuts. Plans unless --apply is supplied."""
import argparse, hashlib, json, os, re, stat, tempfile, uuid
from pathlib import Path
import xml.etree.ElementTree as ET
VERSION='1.3.0'
DESCRIPTION='TrackWerk 1.3.0 by Kid Bunka: eight tracks, 128 synth models, Froggy singing synthesis, sampling and the included Sample Pack.'

def sha(data): return hashlib.sha256(data).hexdigest()
def checked(path):
    path=Path(os.path.abspath(path))
    if any(p.is_symlink() for p in [path,*path.parents]): raise ValueError('Symbolic-link path refused: '+str(path))
    return path

def read(path):
    path=checked(path)
    if path.exists() and not path.is_file(): raise ValueError('Expected regular file: '+str(path))
    return path.read_bytes() if path.exists() else None

def add(changes,path,data):
    path=checked(path)
    if read(path)!=data: changes[path]=data

def plan(card,device,bundle=None):
    card=checked(card);bundle=checked(bundle or Path(__file__).absolute().parent)
    if not card.is_dir():raise ValueError('Select the existing card root')
    spec=json.loads((bundle/'RELEASE-METADATA.json').read_text()); changes={}; programs=[]
    cover=(bundle/'menu-cover-trackwerk-1.3.0.png').read_bytes()
    if sha(cover)!=spec['cover_sha256']:raise ValueError('Menu-cover checksum failed')
    if device.startswith('miyoo'):
        box=(bundle/'boxart-miyoo-1.3.0.png').read_bytes()
        if sha(box)!=spec['miyoo_cover_sha256']:raise ValueError('Miyoo menu-art checksum failed')
        for rel in ['.tmp_update','.tmp_update/script/stop_audioserver.sh','miyoo/app/MainUI']:
            p=checked(card/rel)
            if not (p.is_dir() if rel=='.tmp_update' else p.is_file()):raise ValueError('Incomplete Onion card: '+rel)
        variants=[('miyoo-mini',['TrackWerk','PocketTrackerExtreme'])]
        if device=='miyoo-plus':variants.append(('miyoo-plus',['TrackWerkPlus','PocketTrackerExtremePlusArchive']))
        existing=[]
        for profile,names in variants:
            found=[n for n in names if checked(card/'Roms/PORTS/Games'/n).is_dir()]
            if len(found)>1:raise ValueError('Ambiguous duplicate TrackWerk installations')
            if not found:continue
            name=found[0]; app=card/'Roms/PORTS/Games'/name
            binaries=[p for p in [app/'trackwerk',app/'pockettracker'] if p.is_file()]
            if len(binaries)!=1:raise ValueError('Matching program missing or ambiguous')
            program=checked(binaries[0])
            if sha(program.read_bytes())!=spec['programs'][profile]['sha256']:raise ValueError('Install the matching 1.3.0 program first')
            programs.append(program);existing.append((profile,name,app,program))
        if not existing or not any(p==device for p,_,_,_ in existing):raise ValueError('Matching installation not found')
        for profile,name,app,program in existing:
            suffix=' Offline' if profile=='miyoo-mini' and len(existing)>1 else ''
            title='TrackWerk 1.3'+suffix
            port=card/'Roms/PORTS/Shortcuts'/(title+'.port')
            body='#!/bin/sh\n# TrackWerk 1.3.0 / Kid Bunka\nGameName='+json.dumps(title)+'\nGameDir='+json.dumps(name)+'\nGameExecutable="launch.sh"\nGameDataFile='+json.dumps(program.name)+'\nKillAudioserver=0\nPerformanceMode=0\nArguments=""\n/mnt/SDCARD/Emu/PORTS/launch_standalone.sh "$GameName" "$GameDir" "$GameExecutable" "$Arguments" "$GameDataFile" "$KillAudioserver" "$PerformanceMode"\n'
            add(changes,port,body.encode());add(changes,card/'Roms/PORTS/Imgs'/(title+'.png'),box)
            shortcuts=card/'Roms/PORTS/Shortcuts'
            if shortcuts.is_dir():
                for old in shortcuts.glob('*.port'):
                    if old==port:continue
                    text=read(old).decode('utf-8',errors='replace')
                    if re.search(r'^GameDir=[\"\']?'+re.escape(name)+r'[\"\']?\s*$',text,re.M):add(changes,old,None)
            appdir=card/'App'/name
            config=appdir/'config.json'
            if config.is_file():
                value=json.loads(read(config));value['label']='TrackWerk 1.3.0'+suffix;value['description']=DESCRIPTION
                add(changes,config,(json.dumps(value,indent=2)+'\n').encode())
                add(changes,appdir/'icon.png',box)
                add(changes,appdir/'launch.sh',('#!/bin/sh\n# TrackWerk 1.3.0 / Onion owns audio restoration.\nexec sh /mnt/SDCARD/Roms/PORTS/Games/'+name+'/launch.sh\n').encode())
            add(changes,app/'VERSION.txt',('TrackWerk '+VERSION+'\nProfile: '+profile+'\nProgram SHA-256: '+sha(program.read_bytes())+'\n').encode())
            add(changes,app/'SOURCE.txt',('Corresponding source: TrackWerk-1.3.0-Source.zip\nProgram source: '+spec['program_source_commit']+'\n').encode())
        # Onion owns these disposable lookup databases. Keep a rollback copy rather than deleting them.
        for folder in ['Roms/PORTS','Roms/PORTS/Shortcuts']:
            directory=card/folder
            if directory.is_dir():
                for cache in directory.glob('*_cache*.db'):add(changes,cache,None)
    elif device in ['brick','rgb20s']:
        rel='roms/ports/trackwerk' if device=='brick' else 'ports/trackwerk-amberelec'
        app=checked(card/rel);program=checked(app/'trackwerk.aarch64')
        if not program.is_file() or sha(program.read_bytes())!=spec['programs'][device]['sha256']:raise ValueError('Install the matching 1.3.0 program first')
        programs.append(program)
        add(changes,app/'VERSION.txt',('TrackWerk '+VERSION+'\nProfile: '+device+'\nProgram SHA-256: '+sha(program.read_bytes())+'\n').encode())
        add(changes,app/'SOURCE.txt',('Corresponding source: TrackWerk-1.3.0-Source.zip\nProgram source: '+spec['program_source_commit']+'\n').encode())
        add(changes,app/'menu-cover-trackwerk-1.3.0.png',cover)
        for filename in ['gamelist.xml']:
            menu=checked(app.parent/filename);old=read(menu)
            root=ET.fromstring(old) if old else ET.Element('gameList')
            games=[g for g in root.findall('game') if (g.findtext('path') or '').replace('\\','/').split('/')[-1]=='TrackWerk.sh']
            if not games:
                g=ET.SubElement(root,'game');ET.SubElement(g,'path').text='./TrackWerk.sh';games=[g]
            image='./'+app.name+'/menu-cover-trackwerk-1.3.0.png'
            for game in games:
                for key,text in [('name','TrackWerk 1.3.0'),('desc',DESCRIPTION),('image',image),('thumbnail',image)]:
                    e=game.find(key)
                    if e is None:e=ET.SubElement(game,key)
                    e.text=text
            new=ET.tostring(root,encoding='utf-8',xml_declaration=True)+b'\n'
            add(changes,menu,new)
        info=checked(app/'gameinfo.xml')
        old=read(info)
        if old:
            root=ET.fromstring(old)
            for key,text in [('name','TrackWerk 1.3.0'),('desc',DESCRIPTION),('image','./menu-cover-trackwerk-1.3.0.png'),('thumbnail','./menu-cover-trackwerk-1.3.0.png')]:
                e=root.find(key)
                if e is None:e=ET.SubElement(root,key)
                e.text=text
            add(changes,info,ET.tostring(root,encoding='utf-8',xml_declaration=True)+b'\n')
        launch=app.parent/'TrackWerk.sh';old=read(launch)
        if old:
            new=re.sub(rb'TrackWerk (?:1\.0(?:\.0)?|1\.1(?:\.0)?|1\.2\.[01])(?=[ :/\-\r\n])',b'TrackWerk 1.3.0',old)
            add(changes,launch,new)
    else:raise ValueError('Unknown device')
    return card,programs,changes

def apply(card,programs,changes):
    if not changes:return None
    identities={p:sha(read(p)) for p in programs};before={p:read(p) for p in changes}
    backup=checked(card/'.TrackWerk-Metadata-Backups'/('1.3.0-'+uuid.uuid4().hex));backup.mkdir(parents=True)
    modes={p:stat.S_IMODE(p.stat().st_mode) if p.exists() else (0o755 if p.suffix in ['.sh','.port'] else 0o644) for p in changes}
    for p,data in before.items():
        if data is not None:
            dest=backup/p.relative_to(card);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
    written=[]
    try:
        for p,data in changes.items():
            checked(p)
            if read(p)!=before[p]:raise ValueError('Menu target changed during preparation')
            if any(sha(read(q))!=h for q,h in identities.items()):raise ValueError('Program changed during metadata update')
            p.parent.mkdir(parents=True,exist_ok=True)
            if data is None:p.unlink();written.append(p);continue
            fd,name=tempfile.mkstemp(prefix='.trackwerk-meta-',dir=p.parent);temp=Path(name)
            try:
                with os.fdopen(fd,'wb') as f:f.write(data);f.flush();os.fsync(f.fileno())
                temp.chmod(modes[p]);os.replace(temp,p);written.append(p)
                if read(p)!=data:raise ValueError('Metadata readback failed')
            finally:temp.unlink(missing_ok=True)
    except BaseException:
        for p in reversed(written):
            if read(p)!=changes[p]:continue
            if before[p] is None:p.unlink(missing_ok=True)
            else:p.write_bytes(before[p]);p.chmod(modes[p])
        raise
    return str(backup.relative_to(card))

def refresh(card,device,bundle=None,write=False):
    card,programs,changes=plan(card,device,bundle)
    result={'version':VERSION,'device':device,'metadata':[str(p.relative_to(card)) for p in changes],'applied':write,'user_projects_preferences_changed':False}
    if write:result['metadata_backup']=apply(card,programs,changes)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--card',required=True,type=Path);p.add_argument('--device',required=True,choices=['miyoo-mini','miyoo-plus','brick','rgb20s']);p.add_argument('--apply',action='store_true');a=p.parse_args()
    print(json.dumps(refresh(a.card,a.device,write=a.apply),indent=2))
if __name__=='__main__':main()
