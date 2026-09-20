#!/usr/bin/env python3
"""Build natural/padded ABI-6 commands, measure H7S, restore and verify firmware."""
# Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import runpy
import statistics
import subprocess
import time
from types import SimpleNamespace
import serial

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
HELPER=runpy.run_path(str(ROOT/'tests/command_dispatch/h7s/run.py'))
H7=HELPER['H7']
sha=HELPER['sha']
run=HELPER['run']
def expected(capacity,profile):
    state=0x19a753; total=0
    for i in range(1024):
        state^=(state<<13)&0xffffffff;state^=state>>17;state^=(state<<5)&0xffffffff
        index=0 if profile==0 else i%capacity if profile==1 else state%capacity
        total+=index+1
    return total*32

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--serial',default='002A001F3033510135393935')
    p.add_argument('--port',default='COM6')
    args=p.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=HERE/'CommandStride.cpp'
    receipt=dict(started=datetime.now(timezone.utc).isoformat(),board='NUCLEO-H7S3L8',serial=args.serial,
        base_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        sources={str(f.relative_to(ROOT)):sha(f) for f in [*sorted((ROOT/'lib/telemetry').rglob('*')),source,Path(__file__)] if f.is_file()},
        completed=False,restored_and_verified=False,images=[])
    original=H7['BASE']['prepare']
    for stride in (20,24):
        build=out/str(stride);build.mkdir()
        def prepare(output,variants, stride=stride):
            result=original(output,variants)
            if stride==24:
                header=output/'Current/lib/telemetry/command/TelemetryCommand.h'
                text=header.read_text();marker='    const Describe describe = nullptr;'
                if text.count(marker)!=1:raise RuntimeError('Padding insertion anchor changed')
                header.write_text(text.replace(marker,marker+'\n    const std::uint32_t reservedPadding = 0;'))
            return result
        H7['BASE']['prepare']=prepare
        build_args=SimpleNamespace(cube=HELPER['DEFAULT_CUBE'],arm_cxx=str(HELPER['DEFAULT_COMPILER']),
            variants=['Current'],baseline_ref=None,optimizations=['O2'])
        images=H7['build'](build_args,build,fixture_sources=[source],fixture_inputs=[Path(__file__)])
        for image in images:image['stride']=stride
        receipt['images']+=images
    H7['BASE']['prepare']=original
    receipt['compiler']=(out/'20/common/compiler.log').read_text().splitlines()[0]
    cli=HELPER['DEFAULT_PROGRAMMER']
    connection=['-c','port=SWD','sn='+args.serial,'mode=UR','reset=HWrst','freq=4000']
    def save(): (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    def program(tag,path,raw=False):
        cmd=[cli,*connection,'-w',path,*(['0x08000000'] if raw else []),'-v','-rst']
        if 'Download verified successfully' not in run(cmd,out/(tag+'.log'),60):raise RuntimeError('Unverified flash')
    backup=out/'before.bin'
    run([cli,*connection,'-u','0x08000000','0x10000',backup,'-rst'],out/'backup.log',60)
    if backup.stat().st_size!=65536:raise RuntimeError('Incomplete backup')
    receipt['backup_sha256']=sha(backup);save();rows=[]
    try:
        # Repeat in reverse order to check run order and code/cache variability.
        for session,image in enumerate(receipt['images']+list(reversed(receipt['images']))):
            tag=str(session)+'-'+str(image['stride']);elf=Path(image['elf'])
            if sha(elf)!=image['elf_sha256']:raise RuntimeError('Image changed')
            program('flash-'+tag,elf);print('MEASURING',tag,flush=True)
            observed=set();ready=False;complete=False
            with serial.Serial(args.port,115200,timeout=.5,write_timeout=2) as port, (out/(tag+'-uart.log')).open('w') as log:
                time.sleep(.3);port.reset_input_buffer();port.write(b'R');port.flush();start=time.monotonic()
                while time.monotonic()-start<60:
                    raw=port.readline()
                    if not raw:continue
                    line=raw.decode('ascii').strip();log.write(line+'\n');log.flush();words=line.split()
                    if words[:2]==['STRIDE','READY']:
                        if ready or list(map(int,words[2:]))!=[600000000,image['stride'],32768,9,16]:raise RuntimeError(line)
                        ready=True
                    elif words[:2]==['STRIDE','T']:
                        mem,count,profile,rep,cycles,checksum,result=map(int,words[2:]);key=(mem,count,profile,rep)
                        if (not ready or key in observed or mem not in (0,1) or count not in (128,1024)
                            or profile not in range(3) or rep not in range(9) or result!=0
                            or checksum!=expected(count,profile) or not 0<cycles<32768000):raise RuntimeError(line)
                        observed.add(key);rows.append(dict(session=session,stride=image['stride'],memory=mem,count=count,
                            profile=profile,repetition=rep,cycles=cycles,calls=32768,checksum=checksum,elf_sha256=image['elf_sha256']))
                    elif line=='STRIDE DONE':
                        if len(observed)!=108:raise RuntimeError('Missing windows')
                        complete=True;break
                    else:raise RuntimeError(line)
            if not complete:raise RuntimeError('Board timeout')
            print('PASS',tag,len(observed),flush=True)
        receipt['completed']=True
    finally:
        try:
            program('restore',backup,True);restored=out/'after.bin'
            run([cli,*connection,'-u','0x08000000','0x10000',restored,'-rst'],out/'readback.log',60)
            receipt['restored_sha256']=sha(restored)
            receipt['restored_and_verified']=sha(restored)==receipt['backup_sha256']
            if not receipt['restored_and_verified']:raise RuntimeError('Restoration differs')
            print('RESTORED',sha(restored),flush=True)
        finally:receipt['finished']=datetime.now(timezone.utc).isoformat();save()
    with (out/'samples.csv').open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=rows[0].keys());writer.writeheader();writer.writerows(rows)
    summary=[]
    for mem in (0,1):
        for count in (128,1024):
            for profile in range(3):
                medians=[statistics.median(r['cycles']/r['calls'] for r in rows
                    if (r['memory'],r['count'],r['profile'],r['stride'])==(mem,count,profile,stride)) for stride in (20,24)]
                summary.append(dict(memory=mem,count=count,profile=profile,natural20=medians[0],padded24=medians[1]))
    receipt['summary']=summary;receipt['samples_sha256']=sha(out/'samples.csv');save()
    print(json.dumps(summary,indent=2))
if __name__=='__main__':main()
