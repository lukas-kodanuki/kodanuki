#!/usr/bin/env -S python3 -B
import os
import re
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
NUMBERS = ('tolerance', 'samples', 'batch', 'split', 'surfaces', 'collisions', 'seed', 'threads')
SETTINGS = ('width', 'height', 'faces', 'smooth_glass') + NUMBERS
VIEWER = ('internal_width', 'internal_height')
KEYS = {
    'geometry': ('tree', 'sphere_trace'),
    'execution': ('single', 'packets'),
    'backend': ('cpu',),
    'stopping': ('fixed_count', 'confidence_interval'),
    'detail': ('focus_on_view', 'pilot', 'split_hidden_primitives', 'split_crowded_leaves'),
    'build': ('split_beside_finer', 'cut_long_programs', 'face_slabs', 'shared_coefficients', 'shared_subtrees'),
    'strategy': ('join', 'connect'),
    'sampler': ('sobol', 'highlight_guide', 'each_lamp_first', 'sky_map', 'glowing_surfaces', 'toward_mirrors'),
}
LOOP_KEYS = {
    'source': ('scene', 'flight'),
    'sink': ('file', 'frames', 'window'),
    'observer': ('progress', 'metrics'),
    'reconstruction': ('mean', 'temporal', 'atrous', 'upsample', 'upscale'),
}
ONE = ('geometry', 'execution', 'backend', 'stopping')
BUILT = ('build/faced_hits', 'feature/clouds', 'feature/path_rules', 'feature/coats')
ALWAYS = ('feature/absorption', 'feature/atmosphere', 'feature/dispersion',
    'feature/scattering', 'feature/sky_layers', 'transport/paths')
SWITCHED = {'%s/%s' % (key, member): key for key, members in {**KEYS, **LOOP_KEYS}.items() for member in members}
PLUGINS = sorted(list(SWITCHED) + list(BUILT) + list(ALWAYS))
OFF = ('geometry/sphere_trace', 'execution/single', 'stopping/fixed_count', 'strategy/connect',
    'source/flight', 'sink/frames', 'sink/window', 'observer/metrics',
    'reconstruction/temporal', 'reconstruction/atrous', 'reconstruction/upsample', 'reconstruction/upscale')
DEFAULTS = {name: name not in OFF for name in PLUGINS}


def refused(message):
    print(message, file=sys.stderr)
    raise SystemExit(1)


def checked(path, block, known):
    for key in block:
        if key not in known:
            refused('%s: unknown key %s, known: %s' % (path, key, ', '.join(known)))
    return block


def plugins_of(path, block):
    plugins = {**DEFAULTS, **checked(path, block, PLUGINS)}
    for name, on in plugins.items():
        if not isinstance(on, bool):
            refused('%s: plugins."%s" takes true or false' % (path, name))
        if not on and name in ALWAYS:
            refused('%s: plugins."%s" cannot be left out' % (path, name))
    for key in ONE:
        if [plugins[name] for name, of in SWITCHED.items() if of == key].count(True) != 1:
            refused('%s: exactly one %s plugin is on' % (path, key))
    return plugins


def config_of(path):
    if not path.is_file():
        refused('%s: no such config' % path)
    with open(path, 'rb') as file:
        config = checked(path, tomllib.load(file), ('settings', 'viewer', 'plugins'))
    return final_checked(path, {
        'settings': checked(path, config.get('settings', {}), SETTINGS),
        'viewer': checked(path, config.get('viewer', {}), VIEWER),
        'plugins': plugins_of(path, config.get('plugins', {})),
    })


def pair(flag, settings, keys):
    given = [key for key in keys if key in settings]
    if len(given) == 1:
        refused('config: %s needs %s' % (given[0], ' and '.join(keys)))
    return [flag] + [str(settings[key]) for key in keys] if given else []


def uses(plugins, keys):
    return {key: '+'.join(member for member in members if plugins['%s/%s' % (key, member)]) or 'none' for key, members in keys.items()}


def switches(settings):
    faces = [] if settings.get('faces', True) else ['--use', 'faces=off']
    glass = settings.get('smooth_glass', 'exact')
    return faces + ([] if glass == 'exact' else ['--use', 'smooth_glass=' + glass])


def flags(config, keys):
    settings = config['settings']
    found = pair('--size', settings, ('width', 'height'))
    for key in NUMBERS:
        found += ['--' + key, str(settings[key])] if key in settings else []
    defaults = uses(DEFAULTS, keys)
    for key, value in uses(config['plugins'], keys).items():
        found += ['--use', '%s=%s' % (key, value)] if value != defaults[key] else []
    return found + switches(settings)


def run(command, **extra):
    code = subprocess.run([str(word) for word in command], **extra).returncode
    if code != 0:
        raise SystemExit(code)


def folders():
    return sorted('%s/%s' % (path.parent.name, path.name) for path in (ROOT / 'source' / 'plugins').glob('*/*') if path.is_dir())


def known_plugins():
    present = folders()
    if present != PLUGINS:
        unknown = sorted(set(present) ^ set(PLUGINS))
        refused('workflow.py: the plugins in source/plugins and in PLUGINS differ: %s' % ', '.join(unknown))


def effect(name):
    source = ROOT / 'source' / 'plugins' / name / 'plugin.cpp'
    found = re.search(r'\.effect\s*=\s*Effect::(\w+)', source.read_text()) if source.is_file() else None
    return found.group(1) if found else None


def final_checked(path, config):
    if PACKED or path.name != 'render_final.toml':
        return config
    for name, on in config['plugins'].items():
        if on and effect(name) == 'Biased':
            refused('%s: plugins."%s" is biased, so a final frame leaves it out' % (path, name))
    return config


def built_off(config):
    return [name for name in BUILT if not config['plugins'][name]]


def build_name(config):
    return 'build' + ''.join('-' + name.split('/')[1] for name in built_off(config))


def binaries(config):
    return BINARIES / build_name(config)


def compiled(config, directory, options, targets):
    if PACKED:
        return
    known_plugins()
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
    run([renderer, scene, *flags(config, {**KEYS, **LOOP_KEYS}), '--out', frame.with_suffix('.ppm'), '--pfm', frame.with_suffix('.pfm')])


def viewer(scene, config):
    shown = program(config, 'kodanuki_view')
    internal = pair('--internal', config['viewer'], VIEWER)
    libraries = {'LD_LIBRARY_PATH': str(BINARIES)} if PACKED else {}
    run([shown, scene, *flags(config, KEYS), *internal], env={**os.environ, **libraries})


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
