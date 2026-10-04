#!/usr/bin/env python3
"""Relate current source hashes to unchanged, measured H7S firmware bytes.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
Capture compiles one reproducible fixture object if its __FILE__ path differs.
Neither mode imports a serial package or invokes a device programmer.
"""
import argparse
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import runpy
import subprocess
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SEALED = '7b73fb4c97e48ebb012ea0b6010bc7a4c3c434b3'
SCOPE = 'current source and byte-identical retained H7S images'
EXECUTION = 'offline compile/link only; no device access'
GROUPS = {
    'mcu': ('h7s-own-mcu', ('Mixed', 'Scale')),
    'descriptor': ('h7s-own-descriptor-live', ('Current',)),
    'resources': ('h7s-own-resources-live', ('Current',)),
    'exchange': ('h7s-own-exchange-live', ('Current',)),
    'borrowed': ('h7s-live-qualified', ('Current',)),
}
OFFLINE = 'tests/structured/mcu/local-receipt.json'


def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def lf_sha(path):
    """Hash original text bytes with only CRLF converted to LF."""
    return sha(Path(path).read_bytes().replace(b'\r\n', b'\n'))


def manifest_sha(value):
    return sha(json.dumps(value, sort_keys=True, separators=(',', ':')).encode())


def json_object(pairs):
    value = dict(pairs)
    require(len(value) == len(pairs), 'Duplicate JSON keys')
    return value


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=json_object)


def receipt_path(group):
    return 'tests/structured/' + group + '/h7s/receipt.json'


def expected_keys(group):
    return {(group, family, opt) for family in GROUPS[group][1] for opt in ('O2', 'Os')}


def image_key(group, image):
    return group, image.get('family', image.get('variant', 'Current')), image['optimization']


def provenance(receipt):
    return receipt.get('build_provenance', receipt)


def source_inputs(group, root=ROOT):
    """Mirror the existing build recipes, including relative quoted imports."""
    bench = root / 'tests/structured' / group / 'h7s'
    if group == 'mcu':
        paths = {p for directory in (root / 'lib', bench.parent,
                                    root / 'tests/structured/qualification')
                 for p in directory.rglob('*') if p.is_file() and
                 p.suffix in ('.h', '.hpp', '.cpp', '.c', '.pri', '.py', '.S')}
        paths.update(root / p for p in ('tests/structured/traversal/Fixture.hpp',
            'tests/structured/mcu/h7s/StackCall.S', 'tests/h7s_support/build.py'))
    else:
        paths = {p for p in (root / 'lib').rglob('*') if p.is_file()}
        paths.update(bench / name for name in ('Benchmark.cpp', 'Probe.cpp', 'run.py', 'verify.py'))
        paths.add(root / 'tests/h7s_support/build.py')
        if group == 'borrowed':
            paths.update((bench / 'Fixture.hpp', root / 'lib/telemetry/abi/StructuredAbi.cpp',
                          root / 'tests/structured/mcu/h7s/StackCall.S'))
        else:
            paths.add(bench.parent / 'Fixture.hpp')
        if group == 'descriptor':
            paths.add(root / 'tests/structured/endpoints/MixedFixture.hpp')
        if group == 'resources':
            paths.add(bench / 'Golden.inc')
        if group == 'exchange':
            paths.update(p for p in (root / 'examples/structured_protocol').rglob('*') if p.is_file())
        pending = list(paths)
        while pending:
            path = pending.pop()
            if path.suffix not in ('.cpp', '.hpp', '.h', '.c'):
                continue
            for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"',
                                   path.read_text(encoding='utf-8'), re.M):
                dependency = (path.parent / name).resolve()
                require(dependency.is_relative_to(root.resolve()), 'Quoted input escapes the repository')
                if dependency.is_file() and dependency not in paths:
                    paths.add(dependency)
                    pending.append(dependency)
    return {p.relative_to(root).as_posix(): lf_sha(p) for p in sorted(paths)}


def current_manifests():
    groups = {group: source_inputs(group) for group in GROUPS}
    merged = {}
    for inputs in groups.values():
        merged.update(inputs)
    return groups, {p: h for p, h in merged.items() if p.startswith('lib/')}, {
        p: h for p, h in merged.items() if not p.startswith('lib/')}


def sealed_sources():
    """Read a pinned local Git object; this function performs no network call."""
    result = subprocess.run(['git', 'archive', SEALED], cwd=ROOT, capture_output=True, check=False)
    require(result.returncode == 0,
            'The sealed Git commit is required locally; use a checkout with complete history')
    with tarfile.open(fileobj=io.BytesIO(result.stdout)) as archive:
        blobs = {}
        for member in archive.getmembers():
            if not member.isfile():
                continue
            path = PurePosixPath(member.name)
            require(not path.is_absolute() and '..' not in path.parts, 'Unsafe archived path')
            blobs[member.name] = archive.extractfile(member).read()
    return blobs


def authenticate(manifest, blobs):
    require(manifest, 'Missing historical input manifest')
    for path, digest in manifest.items():
        require(path in blobs and sha(blobs[path].replace(b'\r\n', b'\n')) == digest,
                'Historical source differs from the sealed Git object: ' + path)


def historical_checks(blobs, controls=False):
    """Run original checks against their authenticated tree, with current=True."""
    results = {}
    with tempfile.TemporaryDirectory(prefix='resource-history-') as temporary:
        tree = Path(temporary)
        for relative, data in blobs.items():
            path = tree / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        for group in GROUPS:
            module = runpy.run_path(str(tree / 'tests/structured' / group / 'h7s/verify.py'))
            receipt = read_json(ROOT / receipt_path(group))
            result = module['verify'](receipt)
            if controls:
                if group == 'borrowed':
                    result['controls'] = module['self_test'](receipt['images'])
                elif group == 'mcu':
                    result['controls'] = module['self_test'](receipt)
                else:
                    module['self_test'](receipt)
            results[group] = result
        module = runpy.run_path(str(tree / 'tests/structured/mcu/receipt.py'))
        receipt = read_json(ROOT / OFFLINE)
        results['offline'] = module['verify'](receipt)
        if controls:
            results['offline']['controls'] = module['self_test'](receipt)
    return results


def build_data(build_root, group):
    directory = Path(build_root).resolve() / group
    images = read_json(directory / 'images.json')
    prov = (read_json(directory / 'receipt.json') if group == 'mcu'
            else read_json(directory / 'build-provenance.json'))
    return directory, images, prov


def path_maps(arguments):
    """Parse explicit relocation roots; preserve recorded provenance strings."""
    result = []
    for argument in arguments:
        original, separator, relocated = argument.partition('=')
        require(separator and original and relocated, 'Use --path-map OLD=NEW')
        source, destination = Path(original), Path(relocated)
        require(source.is_absolute() and destination.is_absolute(), 'Path-map roots must be absolute')
        source, destination = source.resolve(), destination.resolve()
        require(source != destination, 'Path-map source and destination must differ')
        require(all(not source.is_relative_to(previous) and not previous.is_relative_to(source)
                    for previous, _ in result), 'Path-map source roots must be distinct and non-overlapping')
        result.append((source, destination))
    return tuple(result)


def artifact_path(directory, value, mappings=()):
    # Windows builders can preserve the long-path drive spelling in provenance.
    # It names the same file; remove only that spelling before confinement checks.
    spelling = str(value)
    if Path('C:/').is_absolute() and re.match(r'^\\\\\?\\[A-Za-z]:\\', spelling):
        spelling = spelling[4:]
    path = Path(spelling)
    if path.is_absolute():
        path = path.resolve()
        for source, destination in mappings:
            if path.is_relative_to(source):
                path = destination / path.relative_to(source)
                break
    else:
        path = directory / path
    path = path.resolve()
    require(path.is_relative_to(directory.resolve()), 'Image path escapes its artifact directory')
    return path


def normalize_fixture_path(build_root, retained_root):
    """Recompile __FILE__ with its measured spelling; never patch an image."""
    directory, images, prov = build_data(build_root, 'exchange')
    old = read_json(ROOT / receipt_path('exchange'))
    measured_inputs = str(PureWindowsPath(old['images'][0]['binary']).parents[2] / 'inputs')
    mapping = '-fmacro-prefix-map=' + str(directory / 'inputs') + '=' + measured_inputs
    compiler = Path(prov['compiler_path'])
    require(sha(compiler.read_bytes()) == prov['compiler_sha256'], 'Compiler artifact changed')
    for image, historical in zip(images, old['images']):
        require(image_key('exchange', image) == image_key('exchange', historical), 'Exchange image order changed')
        binary = artifact_path(directory, image['binary'])
        if sha(binary.read_bytes()) == historical['binary_sha256']:
            continue
        release = binary.parent
        flags = image['flags']
        source = directory / 'inputs/tests/structured/exchange/h7s/Benchmark.cpp'
        obj = release / 'Benchmark.o'
        command = [str(compiler), *flags, mapping, '-c', str(source), '-o', str(obj)]
        log = subprocess.run(command, capture_output=True, text=True, check=True)
        (release / 'macro-path-compile.log').write_text(log.stdout + log.stderr, encoding='utf-8')
        elf = artifact_path(directory, image['elf'])
        common = [directory / 'common' / name for name in image['common_objects_sha256']]
        common = [p for p in common if p.name != 'startup.o'] + [directory / 'common/startup.o']
        # Match the original runner's object and library order exactly.
        command = [str(compiler), '-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
            '-fno-exceptions', '-fno-rtti', *map(str, common), str(release / 'Probe.o'), str(obj),
            '-T' + str(directory / 'common/benchmark.ld'), '--specs=nano.specs', '--specs=nosys.specs',
            '-Wl,--gc-sections', '-Wl,-Map=' + str(release / 'benchmark.map'), '-Wl,--start-group',
            '-lc', '-lm', '-Wl,--end-group', '-o', str(elf)]
        log = subprocess.run(command, capture_output=True, text=True, check=True)
        (release / 'macro-path-link.log').write_text(log.stdout + log.stderr, encoding='utf-8')
        objcopy = compiler.with_name('arm-none-eabi-objcopy' + compiler.suffix)
        subprocess.run([str(objcopy), '-O', 'binary', str(elf), str(binary)], check=True)
        objdump = compiler.with_name('arm-none-eabi-objdump' + compiler.suffix)
        size = compiler.with_name('arm-none-eabi-size' + compiler.suffix)
        for command, name in (([str(objdump), '-h', str(elf)], 'sections.log'),
                              ([str(objdump), '-dr', '-C', str(elf)], 'benchmark.asm'),
                              ([str(size), str(elf)], 'size.log')):
            output = subprocess.run(command, capture_output=True, text=True, check=True)
            (release / name).write_text(output.stdout + output.stderr, encoding='utf-8')
        image['objects_sha256']['Benchmark.o'] = sha(obj.read_bytes())
        image['elf_sha256'], image['binary_sha256'] = sha(elf.read_bytes()), sha(binary.read_bytes())
        image['flash_bytes'] = binary.stat().st_size
        image['stack_usage'] = {p.name: {'sha256': sha(p.read_bytes()), 'bytes': p.stat().st_size}
                                for p in sorted(release.glob('*.su'))}
        image['macro_prefix_map'] = mapping
        require(image['binary_sha256'] == historical['binary_sha256'],
                'Macro path normalization did not reproduce the firmware binary')
    (directory / 'images.json').write_text(json.dumps(images, indent=2) + '\n', encoding='utf-8')


def verify_artifacts(record, build_root, retained_root, mappings=()):
    for group in GROUPS:
        directory, images, prov = build_data(build_root, group)
        require(manifest_sha(prov['input_lf_sha256']) == record['groups'][group]['current_input_manifest_sha256'],
                'Artifact source manifest differs: ' + group)
        for relative, digest in prov['input_lf_sha256'].items():
            require(sha((directory / 'inputs' / relative).read_bytes()) == digest,
                    'Captured artifact source differs: ' + relative)
        compiler = Path(prov['compiler_path'])
        require(compiler.is_file() and sha(compiler.read_bytes()) == record['compiler']['sha256'],
                'Compiler artifact differs')
        scaffold = prov.get('scaffold_sha256', prov.get('scaffold_lf_sha256'))
        for relative, digest in scaffold.items():
            path = directory / 'scaffold' / relative
            actual = lf_sha(path) if group == 'mcu' else sha(path.read_bytes())
            require(actual == digest, 'Copied Cube scaffold differs')
        historical = read_json(ROOT / receipt_path(group))
        retained = Path(retained_root).resolve() / GROUPS[group][0]
        require({image_key(group, i) for i in images} == expected_keys(group), 'Artifact image matrix differs')
        old_images = {image_key(group, i): i for i in historical['images']}
        recorded = {image_key(i['group'], i): i for i in record['images'] if i['group'] == group}
        for image in images:
            key = image_key(group, image)
            old, row = old_images[key], recorded[key]
            current = artifact_path(directory, image['binary'], mappings).read_bytes()
            previous = artifact_path(retained, old['binary'], mappings).read_bytes()
            require(current == previous and sha(current) == row['binary_sha256'] == old['binary_sha256'],
                    'Current/retained raw firmware bytes differ: ' + '/'.join(key))
            require(sha(artifact_path(directory, image['elf'], mappings).read_bytes()) == row['elf_sha256'] and
                    sha(artifact_path(retained, old['elf'], mappings).read_bytes()) == row['retained_elf_sha256'] == old['elf_sha256'],
                    'Current or retained ELF identity differs: ' + '/'.join(key))
            require(len(artifact_path(directory, image['binary'], mappings).read_bytes()) == row['flash_bytes'],
                    'Actual binary length differs')


def verify(record, *, artifacts=None, retained=None, blobs=None, mappings=()):
    require(record['format_version'] == 2 and record['scope'] == SCOPE and record['execution'] == EXECUTION,
            'Wrong equivalence record identity/scope')
    require(record['sealed_code_head'] == SEALED, 'Wrong sealed code commit')
    require(re.fullmatch(r'[0-9a-f]{40}', record['source_head']) and type(record['source_dirty']) is bool,
            'Malformed current source provenance')
    groups, library, fixtures = current_manifests()
    require(record['current_library_lf_sha256'] == library and record['current_fixture_lf_sha256'] == fixtures,
            'Current library/fixture source manifest drift')
    require(set(record['groups']) == set(GROUPS), 'Historical group matrix differs')
    require(re.search(r'14\.3\.1(?:\s|$)', record['compiler']['version']) and
            re.fullmatch(r'[0-9a-f]{64}', record['compiler']['sha256']), 'Wrong compiler identity')
    blobs = sealed_sources() if blobs is None else blobs
    expected = set().union(*(expected_keys(group) for group in GROUPS))
    require(len(record['images']) == 12 and {image_key(row['group'], row) for row in record['images']} == expected,
            'Exactly twelve distinct group/family/optimization images are required')
    for group in GROUPS:
        reference = record['groups'][group]
        path = receipt_path(group)
        require(set(reference) == {'receipt', 'receipt_lf_sha256', 'historical_input_manifest_sha256',
                                   'current_input_manifest_sha256', 'scaffold_manifest_sha256',
                                   'macro_prefix_map', 'image_manifest_sha256'},
                'Historical reference fields differ: ' + group)
        require(reference['receipt'] == path and reference['receipt_lf_sha256'] == lf_sha(ROOT / path),
                'Historical receipt changed: ' + group)
        receipt = read_json(ROOT / path)
        prov = provenance(receipt)
        authenticate(prov['input_lf_sha256'], blobs)
        require(reference['historical_input_manifest_sha256'] == manifest_sha(prov['input_lf_sha256']) and
                reference['current_input_manifest_sha256'] == manifest_sha(groups[group]), 'Input identity differs: ' + group)
        require(record['compiler'] == {'version': prov['compiler'], 'sha256': prov['compiler_sha256']},
                'Current/measured compiler identity differs')
        scaffold = prov.get('scaffold_sha256', prov.get('scaffold_lf_sha256'))
        require(reference['scaffold_manifest_sha256'] == manifest_sha(scaffold), 'Cube scaffold identity differs')
        images = {image_key(group, image): image for image in receipt['images']}
        require(set(images) == expected_keys(group), 'Historical image matrix differs')
        rows = [r for r in record['images'] if r['group'] == group]
        require(reference['image_manifest_sha256'] == manifest_sha(rows), 'Rebuilt image row changed')
        for row in rows:
            historical = images[image_key(group, row)]
            require(set(row) == {'group', 'family', 'optimization', 'flash_bytes', 'binary_sha256',
                                 'elf_sha256', 'retained_elf_sha256'}, 'Image row fields differ')
            require(type(row['flash_bytes']) is int and 0 < row['flash_bytes'] <= 65536,
                    'Invalid firmware image length')
            require(re.fullmatch(r'[0-9a-f]{64}', row['elf_sha256']) and
                    row['retained_elf_sha256'] == historical['elf_sha256'], 'Invalid current/retained ELF identity')
            require(all(row[name] == historical[name] for name in ('flash_bytes', 'binary_sha256')),
                    'Rebuilt image identity differs from the measured receipt')
        if group == 'exchange':
            mapping = reference['macro_prefix_map']
            require(isinstance(mapping, str), 'Exchange fixture mapping is missing')
            match = re.fullmatch(r'-fmacro-prefix-map=(.+)=(.+)', mapping)
            target = str(PureWindowsPath(receipt['images'][0]['binary']).parents[2] / 'inputs')
            require(match and match[2] == target and
                    match[1].replace('\\', '/').endswith('/exchange/inputs'),
                    'Fixture mapping differs from the measured source path')
        else:
            require(reference['macro_prefix_map'] is None, 'Unexpected fixture path mapping')
    require(set(record['offline']) == {'receipt', 'receipt_lf_sha256', 'input_manifest_sha256'},
            'Historical offline reference fields differ')
    require(record['offline']['receipt'] == OFFLINE and
            record['offline']['receipt_lf_sha256'] == lf_sha(ROOT / OFFLINE), 'Historical offline receipt changed')
    offline = read_json(ROOT / OFFLINE)
    authenticate(offline['input_lf_sha256'], blobs)
    require(record['offline']['input_manifest_sha256'] == manifest_sha(offline['input_lf_sha256']),
            'Historical offline input identity differs')
    require((artifacts is None) == (retained is None), 'Supply both rebuilt and retained artifact directories')
    require(artifacts is not None or not mappings, 'Path mapping requires artifact verification')
    if artifacts is not None:
        verify_artifacts(record, artifacts, retained, mappings)
    return {'images': 12, 'groups': 5, 'library_files': len(library), 'fixture_files': len(fixtures),
            'sealed_code_head': SEALED, 'execution': EXECUTION, 'artifact_bytes_rechecked': artifacts is not None}


def capture(build_root, retained_root, mappings=()):
    normalize_fixture_path(build_root, retained_root)
    groups, library, fixtures = current_manifests()
    record = dict(format_version=2, scope=SCOPE, execution=EXECUTION, sealed_code_head=SEALED,
        source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        source_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain', '-uall'], cwd=ROOT, text=True).strip()),
        recorded_at_utc=datetime.now(timezone.utc).isoformat(), current_library_lf_sha256=library,
        current_fixture_lf_sha256=fixtures, groups={}, images=[])
    for group in GROUPS:
        directory, images, prov = build_data(build_root, group)
        require(prov['input_lf_sha256'] == groups[group], 'Build inputs differ from current source: ' + group)
        compiler = {'version': prov['compiler'], 'sha256': prov['compiler_sha256']}
        require('compiler' not in record or record['compiler'] == compiler, 'Rebuilt compiler identities differ')
        record['compiler'] = compiler
        historical = read_json(ROOT / receipt_path(group))
        old = provenance(historical)
        scaffold = prov.get('scaffold_sha256', prov.get('scaffold_lf_sha256'))
        require(scaffold == old.get('scaffold_sha256', old.get('scaffold_lf_sha256')), 'Rebuilt scaffold differs')
        macro_mappings = {i.get('macro_prefix_map') for i in images}
        require(len(macro_mappings) == 1, 'Fixture macro mappings differ between optimizations')
        record['groups'][group] = dict(receipt=receipt_path(group),
            receipt_lf_sha256=lf_sha(ROOT / receipt_path(group)),
            historical_input_manifest_sha256=manifest_sha(old['input_lf_sha256']),
            current_input_manifest_sha256=manifest_sha(prov['input_lf_sha256']),
            scaffold_manifest_sha256=manifest_sha(scaffold), macro_prefix_map=macro_mappings.pop())
        for image in images:
            original = next(i for i in historical['images'] if image_key(group, i) == image_key(group, image))
            record['images'].append(dict(group=group, family=image_key(group, image)[1],
                optimization=image['optimization'], flash_bytes=image['flash_bytes'],
                binary_sha256=image['binary_sha256'], elf_sha256=image['elf_sha256'],
                retained_elf_sha256=original['elf_sha256']))
        record['groups'][group]['image_manifest_sha256'] = manifest_sha(
            [i for i in record['images'] if i['group'] == group])
    offline = read_json(ROOT / OFFLINE)
    record['offline'] = dict(receipt=OFFLINE, receipt_lf_sha256=lf_sha(ROOT / OFFLINE),
                             input_manifest_sha256=manifest_sha(offline['input_lf_sha256']))
    verify(record, artifacts=build_root, retained=retained_root, mappings=mappings)
    return record


def relocation_self_test():
    """Relocation reads stay inside the requested per-group artifact directory."""
    rejected, positive = 0, 2
    with tempfile.TemporaryDirectory(prefix='resource-paths-') as temporary:
        base = Path(temporary).resolve()
        source, destination = base / 'old-build', base / 'archive'
        directory = destination / 'group'
        directory.mkdir(parents=True)
        image = directory / 'image.bin'
        image.write_bytes(b'path-control')
        mappings = path_maps([str(source) + '=' + str(destination)])
        require(artifact_path(directory, str(source / 'group/image.bin'), mappings) == image,
                'Valid relocated artifact refused')
        require(artifact_path(directory, 'image.bin', mappings) == image,
                'Relative artifact changed by a path mapping')
        if Path('C:/').is_absolute():
            require(artifact_path(directory, '\\\\?\\' + str(image), mappings) == image,
                    'Extended Windows drive spelling changed artifact identity')
            positive += 1
        cases = [
            lambda: path_maps(['relative=' + str(destination)]),
            lambda: path_maps([str(source) + '=relative']),
            lambda: path_maps([str(source) + '=' + str(source)]),
            lambda: path_maps([str(source) + '=' + str(destination)] * 2),
            lambda: path_maps([str(source) + '=' + str(destination),
                               str(source / 'group') + '=' + str(destination)]),
            lambda: artifact_path(directory, '../image.bin', mappings),
            lambda: artifact_path(directory, str(source / 'group/image.bin')),
            lambda: artifact_path(directory, str(base / 'old-build-other/group/image.bin'), mappings),
            lambda: artifact_path(directory, str(source / 'group/image.bin'),
                                   path_maps([str(source) + '=' + str(base / 'elsewhere')])),
        ]
        if Path('C:/').is_absolute():
            cases.append(lambda: artifact_path(directory, '\\\\?\\' + str(base / 'outside.bin'), mappings))
        for case in cases:
            try:
                case()
            except RuntimeError:
                rejected += 1
                continue
            raise AssertionError('An invalid artifact relocation was accepted')
    return {'positive_paths': positive, 'rejected_mutations': rejected}


def self_test(record):
    blobs = sealed_sources()
    verify(record, blobs=blobs)
    mutations = [
        ('old format', lambda r: r.update(format_version=1)),
        ('scope', lambda r: r.update(execution='device')),
        ('sealed commit', lambda r: r.update(sealed_code_head='0' * 40)),
        ('missing library input', lambda r: r['current_library_lf_sha256'].pop(next(iter(r['current_library_lf_sha256'])))),
        ('changed library input', lambda r: r['current_library_lf_sha256'].__setitem__(next(iter(r['current_library_lf_sha256'])), '0' * 64)),
        ('extra library input', lambda r: r['current_library_lf_sha256'].__setitem__('lib/extra.hpp', '0' * 64)),
        ('missing fixture input', lambda r: r['current_fixture_lf_sha256'].pop(next(iter(r['current_fixture_lf_sha256'])))),
        ('missing group', lambda r: r['groups'].pop('exchange')),
        ('missing image', lambda r: r['images'].pop()),
        ('duplicate image', lambda r: r['images'].__setitem__(1, deepcopy(r['images'][0]))),
        ('changed family', lambda r: r['images'][0].update(family='Other')),
        ('changed optimization', lambda r: r['images'][0].update(optimization='Og')),
        ('changed binary', lambda r: r['images'][0].update(binary_sha256='0' * 64)),
        ('changed ELF', lambda r: r['images'][0].update(elf_sha256='0' * 64)),
        ('changed retained ELF', lambda r: r['images'][0].update(retained_elf_sha256='0' * 64)),
        ('changed length', lambda r: r['images'][0].update(flash_bytes=1)),
        ('changed receipt', lambda r: r['groups']['borrowed'].update(receipt_lf_sha256='0' * 64)),
        ('legacy receipt digest', lambda r: r['groups']['borrowed'].update(receipt_sha256='0' * 64)),
        ('changed historical manifest', lambda r: r['groups']['mcu'].update(historical_input_manifest_sha256='0' * 64)),
        ('changed build manifest', lambda r: r['groups']['mcu'].update(current_input_manifest_sha256='0' * 64)),
        ('changed image manifest', lambda r: r['groups']['mcu'].update(image_manifest_sha256='0' * 64)),
        ('changed scaffold', lambda r: r['groups']['descriptor'].update(scaffold_manifest_sha256='0' * 64)),
        ('changed compiler', lambda r: r['compiler'].update(sha256='0' * 64)),
        ('missing fixture mapping', lambda r: r['groups']['exchange'].update(macro_prefix_map=None)),
        ('changed fixture mapping', lambda r: r['groups']['exchange'].update(macro_prefix_map='-fmacro-prefix-map=x=y')),
        ('changed offline receipt', lambda r: r['offline'].update(receipt_lf_sha256='0' * 64)),
        ('changed offline manifest', lambda r: r['offline'].update(input_manifest_sha256='0' * 64)),
    ]
    for label, mutation in mutations:
        changed = deepcopy(record)
        mutation(changed)
        try:
            verify(changed, blobs=blobs)
        except RuntimeError:
            continue
        raise AssertionError('Accepted equivalence mutation: ' + label)
    return {'rejected_mutations': len(mutations), 'relocation': relocation_self_test(),
            'text_identity': text_identity_self_test()}


def text_identity_self_test():
    """Text checkout conventions do not change receipt identity or binary hashes."""
    with tempfile.TemporaryDirectory(prefix='resource-text-') as temporary:
        path = Path(temporary) / 'receipt.json'
        original = b'{\n  "count": 12\n}\n'
        path.write_bytes(original)
        digest = lf_sha(path)
        path.write_bytes(original.replace(b'\n', b'\r\n'))
        require(lf_sha(path) == digest, 'CRLF checkout changed text identity')
        require(sha(path.read_bytes()) != sha(original), 'Raw artifact hashes were normalized')
        path.write_bytes(b'{"count": 12}\n')
        require(lf_sha(path) != digest, 'Text identity ignored JSON formatting changes')
        path.write_bytes(original.replace(b'12', b'13'))
        require(lf_sha(path) != digest, 'Text identity ignored a measurement change')
    return {'checkout_equivalence': 1, 'distinct_changes': 3}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest='mode', required=True)
    create = modes.add_parser('capture')
    create.add_argument('--build-root', type=Path, required=True)
    create.add_argument('--retained-root', type=Path, required=True)
    create.add_argument('--output', type=Path, default=HERE / 'equivalence.json')
    create.add_argument('--path-map', action='append', default=[], metavar='OLD=NEW',
                        help='Explicit relocation for reading retained artifact paths')
    check = modes.add_parser('verify')
    check.add_argument('--record', type=Path, default=HERE / 'equivalence.json')
    check.add_argument('--build-root', type=Path)
    check.add_argument('--retained-root', type=Path)
    check.add_argument('--path-map', action='append', default=[], metavar='OLD=NEW',
                       help='Explicit absolute artifact-root relocation; recorded paths are not changed')
    check.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.mode == 'capture':
        mappings = path_maps(args.path_map)
        record = capture(args.build_root, args.retained_root, mappings)
        args.output.write_text(json.dumps(record, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        print(json.dumps(verify(record, artifacts=args.build_root, retained=args.retained_root,
                               mappings=mappings), sort_keys=True))
    else:
        record = read_json(args.record)
        result = verify(record, artifacts=args.build_root, retained=args.retained_root,
                        mappings=path_maps(args.path_map))
        result['historical'] = historical_checks(sealed_sources(), args.self_test)
        if args.self_test:
            result['controls'] = self_test(record)
        print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
