#!/usr/bin/env -S python3 -B
import os
import shutil
import subprocess
import sys
import tomllib
import zipfile
from pathlib import Path

TARGET = Path(__file__).resolve().parent
ROOT = TARGET.parent
OUTPUT = ROOT / 'output'
PACKED = not (TARGET / 'CMakeLists.txt').exists()
BINARIES = ROOT / 'bin' if PACKED else OUTPUT
VERSION = '0.0.3'
USAGE = 'usage: workflow.py test|render|viewer|export|clean scene.sdf config.toml'
NUMBERS = ('tolerance', 'samples', 'batch', 'surfaces', 'collisions', 'seed', 'threads')
SETTINGS = ('width', 'height', 'faces', 'smooth_glass') + NUMBERS
VIEWER = ('internal_width', 'internal_height')
JOINED = {
    'detail': (['by_distance'], {
        'detail_correct_focus_on_view': 'focus_on_view',
        'detail_correct_pilot': 'pilot',
        'detail_correct_split_hidden_primitives': 'split_hidden_primitives',
        'detail_correct_split_crowded_leaves': 'split_crowded_leaves',
    }),
    'passes': ([], {
        'build_correct_split_beside_finer': 'split_beside_finer',
        'build_correct_cut_long_programs': 'cut_long_programs',
        'build_correct_face_slabs': 'face_slabs',
        'build_correct_shared_coefficients': 'shared_coefficients',
        'build_correct_shared_subtrees': 'shared_subtrees',
    }),
    'strategies': (['meet', 'aim'], {
        'strategy_correct_join': 'join',
        'strategy_correct_connect': 'connect',
    }),
    'aims': (['lamps'], {
        'sampler_correct_sky_map': 'sky_map',
        'sampler_correct_glowing_surfaces': 'glowing_surfaces',
    }),
}
CHOSEN = {
    'execution_correct_waves': ('execution', 'waves', 'one_at_a_time'),
    'stopping_consistent_confidence_interval': ('stopping', 'confidence', 'fixed'),
    'sampler_correct_sobol': ('sequence', 'sobol', 'independent'),
    'sampler_correct_highlight_guide': ('direction', 'material+highlight_guide', 'material'),
    'sampler_correct_each_lamp_first': ('selection', 'each_lamp_first', 'one_by_contribution'),
}
BUILT = ('build_correct_faced_hits', 'feature_biased_clouds', 'feature_biased_path_rules', 'feature_mixed_coats')
ALWAYS = ('feature_correct_absorption', 'feature_correct_atmosphere', 'feature_correct_dispersion',
    'feature_correct_scattering', 'feature_correct_sky_layers', 'reconstruction_biased_atrous',
    'reconstruction_biased_temporal', 'reconstruction_biased_upsample', 'reconstruction_biased_upscale',
    'sampler_correct_toward_mirrors')
EXTRAS = sorted([name for _, members in JOINED.values() for name in members] + list(CHOSEN) + list(BUILT) + list(ALWAYS))
DEFAULTS = {name: name != 'strategy_correct_connect' for name in EXTRAS}


def refused(message):
    print(message, file=sys.stderr)
    raise SystemExit(1)


def checked(path, block, known):
    for key in block:
        if key not in known:
            refused('%s: unknown key %s, known: %s' % (path, key, ', '.join(known)))
    return block


def extras_of(path, block):
    extras = {**DEFAULTS, **checked(path, block, EXTRAS)}
    for name, on in extras.items():
        if not isinstance(on, bool):
            refused('%s: extras.%s takes true or false' % (path, name))
        if not on and name in ALWAYS:
            refused('%s: extras.%s cannot be left out' % (path, name))
    return extras


def config_of(path):
    if not path.is_file():
        refused('%s: no such config' % path)
    with open(path, 'rb') as file:
        config = checked(path, tomllib.load(file), ('settings', 'viewer', 'extras'))
    return {
        'settings': checked(path, config.get('settings', {}), SETTINGS),
        'viewer': checked(path, config.get('viewer', {}), VIEWER),
        'extras': extras_of(path, config.get('extras', {})),
    }


def pair(flag, settings, keys):
    given = [key for key in keys if key in settings]
    if len(given) == 1:
        refused('config: %s needs %s' % (given[0], ' and '.join(keys)))
    return [flag] + [str(settings[key]) for key in keys] if given else []


def uses(extras):
    found = {}
    for key, (base, members) in JOINED.items():
        chosen = base + [member for name, member in members.items() if extras[name]]
        found[key] = '+'.join(chosen) or 'none'
    for name, (key, on, off) in CHOSEN.items():
        found[key] = on if extras[name] else off
    return found


def switches(settings):
    faces = [] if settings.get('faces', True) else ['--use', 'faces=off']
    glass = settings.get('smooth_glass', 'exact')
    return faces + ([] if glass == 'exact' else ['--use', 'smooth_glass=' + glass])


def flags(config):
    settings = config['settings']
    found = pair('--size', settings, ('width', 'height'))
    for key in NUMBERS:
        found += ['--' + key, str(settings[key])] if key in settings else []
    defaults = uses(DEFAULTS)
    for key, value in uses(config['extras']).items():
        found += ['--use', '%s=%s' % (key, value)] if value != defaults[key] else []
    return found + switches(settings)


def run(command, **extra):
    code = subprocess.run([str(word) for word in command], **extra).returncode
    if code != 0:
        raise SystemExit(code)


def known_extras():
    present = sorted(path.name for path in (ROOT / 'source' / 'extras').iterdir() if path.is_dir())
    if present != EXTRAS:
        unknown = sorted(set(present) ^ set(EXTRAS))
        refused('workflow.py: the extras in source/extras and in EXTRAS differ: %s' % ', '.join(unknown))


def built_off(config):
    return [name for name in BUILT if not config['extras'][name]]


def build_name(config):
    return 'build' + ''.join('-' + name.split('_', 2)[2] for name in built_off(config))


def binaries(config):
    return BINARIES / build_name(config)


def compiled(config, directory, options, targets):
    if PACKED:
        return
    known_extras()
    off = ' '.join(built_off(config))
    run(['cmake', '-S', TARGET, '-B', directory, '-G', 'Ninja', '-DOFF=' + off, *options], stdout=subprocess.DEVNULL)
    rules = (directory / 'build.ninja').read_text()
    for target in targets:
        if 'build %s:' % target not in rules:
            refused('%s: SDL3 was not found, so it cannot be built' % target)
    run(['ninja', '-C', directory, *targets])


def unpacked(command):
    if PACKED:
        refused('%s: only in the repository' % command)


def program(config, name):
    compiled(config, binaries(config), (), (name,))
    return binaries(config) / name


def test(scene, config):
    unpacked('test')
    run([program(config, 'kodanuki_tests')])


def render(scene, config):
    renderer = program(config, 'kodanuki')
    OUTPUT.mkdir(exist_ok=True)
    frame = OUTPUT / scene.stem
    run([renderer, scene, *flags(config), '--out', frame.with_suffix('.ppm'), '--pfm', frame.with_suffix('.pfm')])


def viewer(scene, config):
    shown = program(config, 'kodanuki_view')
    internal = pair('--internal', config['viewer'], VIEWER)
    libraries = {'LD_LIBRARY_PATH': str(BINARIES)} if PACKED else {}
    run([shown, scene, *flags(config), *internal], env={**os.environ, **libraries})


def clean(scene, config):
    shutil.rmtree(OUTPUT, ignore_errors=True)


def linked(binary):
    listed = subprocess.run(['ldd', binary], capture_output=True, text=True).stdout
    words = [line.split() for line in listed.splitlines()]
    return [(Path(word[2]), word[0]) for word in words if len(word) >= 3 and word[0].startswith('libSDL') and word[1] == '=>']


def programs(directory):
    return [(directory / name, 'bin/%s/%s' % (directory.name, name)) for name in ('kodanuki', 'kodanuki_view')]


def packed(directories):
    found = [entry for directory in directories for entry in programs(directory)]
    libraries = [(path, 'bin/' + name) for path, name in linked(directories[0] / 'kodanuki_view')]
    configs = [(path, 'target/configs/' + path.name) for path in sorted((TARGET / 'configs').glob('*.toml'))]
    scenes = [(path, 'scenes/' + path.name) for path in sorted((ROOT / 'scenes').glob('*.sdf'))]
    carried = [(TARGET / 'workflow.py', 'target/workflow.py'), (TARGET / 'export' / 'Makefile', 'Makefile'), (TARGET / 'export' / 'README.md', 'README.md')]
    return found + libraries + configs + scenes + carried


def zipped(out, name, files):
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path, inside in files:
            entry = zipfile.ZipInfo(name + '/' + inside)
            entry.external_attr = (0o100755 if os.access(path, os.X_OK) else 0o100644) << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, path.read_bytes())


def variants():
    configs = [config_of(path) for path in sorted((TARGET / 'configs').glob('*.toml'))]
    return list({build_name(config): config for config in configs}.items())


def export(scene, config):
    unpacked('export')
    portable = ['-DKODANUKI_PORTABLE=ON', '-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc -s']
    directories = []
    for named, variant in variants():
        directory = OUTPUT / 'export' / named
        compiled(variant, directory, portable, ('kodanuki', 'kodanuki_view'))
        directories.append(directory)
    name = 'kodanuki-' + VERSION
    out = OUTPUT / (name + '.zip')
    zipped(out, name, packed(directories))
    print('wrote %s, %.1f MB' % (out.relative_to(ROOT), out.stat().st_size / 1e6))


COMMANDS = {
    'test': test,
    'render': render,
    'viewer': viewer,
    'export': export,
    'clean': clean,
}


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in COMMANDS:
        refused(USAGE)
    scene = Path(sys.argv[2]).resolve()
    config = config_of(Path(sys.argv[3]))
    os.chdir(ROOT)
    COMMANDS[sys.argv[1]](scene, config)


main()
