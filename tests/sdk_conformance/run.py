#!/usr/bin/env python3
"""Four API surfaces over one real Model through relocated installed SDKs. MIT."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
COMPONENTS = ('core;encoded_backend;encoded_server;batch_server;read_many_static_client;'
              'batch_static_client;read_many_client;batch_client')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot():
    paths = [ROOT/'CMakeLists.txt', ROOT/'VERSION']
    suffixes = {'.hpp','.h','.cpp','.py','.txt','.cmake','.in','.pri','.ts','.js','.mjs','.json'}
    for folder in ('lib','cmake','tools/telemetry_codegen','tests/sdk_conformance'):
        paths += [p for p in (ROOT/folder).rglob('*') if p.is_file() and p.suffix in suffixes
                  and '__pycache__' not in p.parts]
    paths += [ROOT/'tests/batch_static'/name for name in ('Model.hpp','Descriptor.cpp')]
    paths += [ROOT/'tests/client_dynamic_web/Bridge.cpp']
    paths += [ROOT/'web'/name for name in ('DynamicClient.js','DynamicClient.d.ts','telemetry.js','telemetry.d.ts')]
    return {p.relative_to(ROOT).as_posix():digest(p) for p in sorted(set(paths))}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--node',required=True)
    parser.add_argument('--tsc',required=True)
    parser.add_argument('--cxx',default=os.environ.get('CXX','g++'))
    parser.add_argument('--cmake',default='cmake')
    parser.add_argument('--ninja',default='ninja')
    parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args()
    out=args.build_dir.resolve()
    if out==ROOT or ROOT in out.parents or out in ROOT.parents:
        parser.error('Use an external dedicated output directory')
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        parser.error('Use a fresh empty output directory; nothing is deleted')
    out.mkdir(parents=True,exist_ok=True)
    (out/'package.json').write_text('{"type":"module"}\n')
    tools={key:Path(shutil.which(getattr(args,key)) or getattr(args,key)).absolute()
           for key in ('cxx','cmake','ninja','node')}
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
             UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    report=dict(passed=False,hardware=False,installed_consumer=True,sanitize=args.sanitize,
        started_utc=datetime.now(timezone.utc).isoformat(),source_sha256_before=snapshot(),
        commands=[],executions={},executables={})

    def save():
        (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')

    def run(name,command,floor=None):
        command=list(map(str,command)); start=time.monotonic()
        result=subprocess.run(command,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=600)
        log=out/(name+'.log'); log.write_bytes(result.stdout)
        report['commands'].append(dict(name=name,command=command,exit=result.returncode,
            seconds=time.monotonic()-start,log=log.name,sha256=digest(log)))
        save()
        print(name+': exit '+str(result.returncode),flush=True)
        if result.returncode:
            raise RuntimeError(name+' failed\n'+result.stdout.decode(errors='replace')[-16000:])
        output=result.stdout.decode('utf8')
        if floor is not None:
            counts=re.findall(r'checks=(\d+)(?: packets=\d+)? failures=0',output)
            if len(counts)!=1 or int(counts[0])<floor:
                raise RuntimeError('Missing meaningful counted result: '+name)
            report['executions'][name]=int(counts[0])
        return output

    def binary(name,path):
        report['executables'][name]=dict(path=path.relative_to(out).as_posix(),sha256=digest(path),bytes=path.stat().st_size)

    try:
        report['source_head']=run('head',['git','rev-parse','HEAD']).strip()
        report['source_status']=run('status',['git','status','--porcelain'])
        for name,tool in tools.items(): run(name+'-version',[tool,'--version'])
        run('typescript-version',[tools['node'],args.tsc,'--version'])
        report['tool_sha256']={name:digest(path.resolve()) for name,path in tools.items()}
        report['typescript_sha256']=digest(Path(args.tsc))
        flags='-Wall -Wextra -Werror -fstack-usage'
        options=['-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
            '-DCMAKE_CXX_COMPILER='+str(tools['cxx']),'-DCMAKE_MAKE_PROGRAM='+str(tools['ninja']),
            '-DCMAKE_CXX_FLAGS_RELEASE='+('-O1 -DNDEBUG' if args.sanitize else '-O2 -DNDEBUG')]
        if args.sanitize:
            flags+=' -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
            options+=['-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
        options+=['-DCMAKE_CXX_FLAGS='+flags]
        prefix=out/'installed'
        run('package-configure',[tools['cmake'],'-S',ROOT,'-B',out/'package',*options,
            '-DTELEMETRY_COMPONENTS='+COMPONENTS,'-DCMAKE_INSTALL_PREFIX='+str(prefix)])
        run('package-build',[tools['cmake'],'--build',out/'package','-j2'])
        run('package-install',[tools['cmake'],'--install',out/'package'])
        relocated=out/'relocated'; prefix.rename(relocated)
        config='-DTelemetry_DIR='+str(relocated/'lib/cmake/Telemetry')
        run('producer-configure',[tools['cmake'],'-S',HERE,'-B',out/'producer',*options,config,
            '-DCONFORMANCE_PRODUCER_ONLY=ON'])
        run('producer-build',[tools['cmake'],'--build',out/'producer','-j2'])
        suffix='.exe' if os.name=='nt' else ''
        descriptor=out/'descriptor.bin'
        producer=out/'producer'/('conformance-descriptor'+suffix)
        run('descriptor',[producer,descriptor]); binary('descriptor-producer',producer)
        generated=out/'generated'
        generation=[sys.executable,'-B','-m','tools.telemetry_codegen','generate',descriptor,
            '--targets','cpp,js,ts','--namespace','conformance_api','--structural-metadata','--output',generated]
        run('generate',generation); run('generate-check',[*generation,'--check'])
        run('compile-typescript',[tools['node'],args.tsc,generated/'ts/device.ts',generated/'ts/device.metadata.ts',
            '--strict','--target','ES2022','--module','ES2022','--declaration','--outDir',generated/'compiled'])
        consumer=out/'consumer'
        run('consumer-configure',[tools['cmake'],'-S',HERE,'-B',consumer,*options,config,
            '-DCONFORMANCE_GENERATED='+str(generated/'cpp')])
        run('consumer-build',[tools['cmake'],'--build',consumer,'-j2'])
        for build in (out/'producer',consumer):
            for entry in json.loads((build/'compile_commands.json').read_text()):
                command=entry['command'].replace('\\','/').lower()
                if (ROOT/'lib').as_posix().lower() in command or (relocated/'include').as_posix().lower() not in command:
                    raise RuntimeError('Consumer escaped relocated installed include roots')
        native=consumer/('conformance-native'+suffix); peer=consumer/('conformance-peer'+suffix)
        binary('native',native); binary('peer',peer)
        native_report=out/'native.json'
        run('native',[native,native_report],321)
        trm,tbm=out/'trm1.bin',out/'tbm1.bin'
        run('read-many-capability',[peer,'--read-many-capability',trm])
        run('batch-capability',[peer,'--capability',tbm])
        peer_descriptor=out/'peer-descriptor.bin'
        run('peer-descriptor',[peer,'--descriptor',peer_descriptor])
        if peer_descriptor.read_bytes()!=descriptor.read_bytes():
            raise RuntimeError('Producer and native peer did not use the identical Descriptor')
        runtime=out/'runtime'
        run('single-source-runtime',[sys.executable,'-B',ROOT/'lib/client/web/build.py',
            '--node',tools['node'],'--tsc',args.tsc,'--output',runtime])
        for label,core,sdk in [('js',ROOT/'lib/client/web',generated/'js'),('ts',runtime,generated/'compiled')]:
            local=out/('consumer-'+label)
            (local/'lib/client/web').mkdir(parents=True); (local/'web').mkdir(); (local/'generated').mkdir()
            for name in ('Client.js','Client.d.ts'): shutil.copy2(core/name,local/'lib/client/web'/name)
            for name in ('DynamicClient.js','DynamicClient.d.ts','telemetry.js','telemetry.d.ts'):
                shutil.copy2(ROOT/'web'/name,local/'web'/name)
            for name in ('device.js','device.d.ts','device.metadata.js','device.metadata.d.ts'):
                shutil.copy2(sdk/name,local/'generated'/name)
            output=out/('conformance-'+label+'.json')
            run('conformance-'+label,[tools['node'],HERE/'Conformance.mjs',local/'web/DynamicClient.js',
                local/'lib/client/web/Client.js',local/'generated/device.js',descriptor,trm,tbm,peer,native_report,
                generated/'metadata/schema.json',local/'generated/device.metadata.js',output],287)
            shutil.copy2(HERE/'Consumer.ts',local/'Consumer.ts')
            run('strict-consumer-'+label,[tools['node'],args.tsc,local/'Consumer.ts','--strict',
                '--noUnusedLocals','--noUnusedParameters','--target','ES2022','--module','ES2022','--noEmit'])
            report[label+'_conformance']=json.loads(output.read_text())
        report['installed_sha256']={p.relative_to(relocated).as_posix():digest(p) for p in relocated.rglob('*') if p.is_file()}
        report['generated_sha256']={p.relative_to(generated).as_posix():digest(p) for p in generated.rglob('*') if p.is_file()}
        report['source_sha256_after']=snapshot()
        if report['source_sha256_before']!=report['source_sha256_after']:
            raise RuntimeError('Source inputs changed during qualification')
        report['checks']=sum(report['executions'].values()); report['passed']=True
    except Exception as error:
        report['error']=str(error); raise
    finally:
        report['finished_utc']=datetime.now(timezone.utc).isoformat(); save()
    print('Four-surface installed conformance passed:',report['executions'],flush=True)


if __name__=='__main__':
    main()
