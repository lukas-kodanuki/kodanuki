#!/usr/bin/env -S python3 -B
"""Whether a build draws the same image within noise, and what it costs.

  within-noise.py baseline <scene> [--quick] [--track]
  within-noise.py check    <scene> [--quick] [--track] [--use key=value]... [--seed n] [--pfm image.pfm --samples n]
  within-noise.py budget   <scene> [--quick] [--last] [--use key=value]...

A scene is a name in scenes/ (gallery) or a path. The renderer is the
build of the worktree this file lies in (--binary names another); the
tool never builds. The settings are those of render_final.toml: its
size (full) or 480 x 300 (--quick), its tolerance, 5 threads.

baseline  draws four runs (seeds 1 to 4) and a reference (seed 6,
          --samples 4096 --use stopping=fixed_count at the same tolerance) and
          records instructions (perf stat -e instructions:u), wall time,
          peak resident memory and samples a pixel in baseline.json. It
          writes into $KODANUKI_BASELINE if that is set, else into
          output/baseline/, as <scene>/<size>/, complete or not at all.
check     draws one candidate (seed 5) with the current build, or takes
          --pfm, and compares it against the baseline in
          $KODANUKI_BASELINE (default ~/Downloads/kodanuki-baseline).
          Exits 1 when a check fails.
budget    draws seed 1 (or takes the run the last check drew, --last)
          and exits 1 when instructions or peak memory are beyond 2.0
          times the mean of the baseline's four runs. Where the run
          wrote a metrics report (--use observer=metrics+progress), it
          prints the report's seconds by step and plugin and its
          counters.

The three checks:
  noise   The RMSE against the reference, over all pixels and the three
          channels, in steps of a byte after the grade (filmic, exposure
          1, as compare.py), which is the unit of the renderer's
          tolerance. Passes at no more than 1.05 times the largest of
          the baseline's four.
  bias    Linear luminance, blocks of 16 x 16 pixels. A block's
          difference is the mean of image - reference over it. Its
          standard error comes from three baseline runs: the variance
          between them at every pixel, summed over the block (pixels
          taken as independent), at least 1e-5 of the reference. A
          baseline run is judged by the other three runs; the candidate
          by each of the four threes, averaged, so both are judged
          alike. Passes when the share of blocks beyond three standard
          errors is at most the baseline's share plus one point.
  effort  Mean samples a pixel within 10% of the mean of the baseline's
          four runs.
The difference map shows the candidate's block differences in standard
errors (of all four runs): red above the reference, blue below, full
colour at six.

--track   a clip of 8 frames with the viewer's reconstruction
          (--reconstruction temporal+denoise+upscale --factor 4), one
          sample a pixel each (--samples 1 --use stopping=fixed_count):
          a scene beside the run imports the scene and adds the clip and
          a camera of stops; the camera holds for four frames, then turns
          half a degree a frame around its target. The reference is the
          mean of 32 more runs (seeds 101 to 132). Noise and bias are
          taken over all frames; effort is not judged, as every frame
          draws one sample a pixel.
"""
import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
import time
import tomllib
from pathlib import Path

import numpy

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / 'target' / 'configs' / 'render_final.toml'
SHARED = Path.home() / 'Downloads' / 'kodanuki-baseline'
BUILT = ('build/faced_hits', 'feature/clouds', 'feature/path_rules', 'feature/coats')
NUMBERS = ('tolerance', 'samples', 'batch', 'split', 'surfaces', 'collisions')
QUICK = (480, 300)
THREADS = 5
SEEDS = (1, 2, 3, 4)
CANDIDATE_SEED = 5
REFERENCE_SEED = 6
REFERENCE = ['--samples', '4096', '--use', 'stopping=fixed_count']
FRAMES = 8
TRACK = ['--reconstruction', 'temporal+denoise+upscale', '--factor', '4']
PER_FRAME = ['--samples', '1', '--use', 'stopping=fixed_count']
TRACK_SEEDS = range(101, 133)
TURN = 0.5
BLOCK = 16
NOISE_LIMIT = 1.05
BIAS_ERRORS = 3
BIAS_LIMIT = 0.01
EFFORT_LIMIT = 0.10
BUDGET_LIMIT = 2.0
FLOOR = 1e-5
LIGHT = numpy.array([0.2126, 0.7152, 0.0722])


def refused(message):
    print(message, file=sys.stderr)
    raise SystemExit(1)


# ---------------------------------------------------------------- images

def read_pfm(path):
    _, size, scale, body = Path(path).read_bytes().split(b'\n', 3)
    width, height = map(int, size.split())
    pixels = numpy.frombuffer(body[:width * height * 12], dtype='<f4' if float(scale) < 0 else '>f4')
    return pixels.reshape(height, width, 3)[::-1].astype(numpy.float64)


def write_pfm(path, image):
    height, width = image.shape[:2]
    with open(path, 'wb') as file:
        file.write(b'PF\n%d %d\n-1.0\n' % (width, height))
        file.write(image[::-1].astype('<f4').tobytes())


def write_ppm(path, image):
    height, width = image.shape[:2]
    with open(path, 'wb') as file:
        file.write(b'P6 %d %d 255\n' % (width, height))
        file.write((image * 255).clip(0, 255).astype(numpy.uint8).tobytes())


def steps(image):
    v = numpy.maximum(image, 0)
    filmic = numpy.where(v >= 16, 2.51 / 2.43, v * (2.51 * v + 0.03) / (v * (2.43 * v + 0.59) + 0.14))
    return numpy.clip(filmic, 0, 1) ** (1 / 2.2) * 255


def block_sums(values):
    rows = numpy.arange(0, values.shape[0], BLOCK)
    columns = numpy.arange(0, values.shape[1], BLOCK)
    return numpy.add.reduceat(numpy.add.reduceat(values, rows, axis=0), columns, axis=1)


def block_error(variance, count, floor):
    return numpy.maximum(numpy.sqrt(numpy.maximum(block_sums(variance), 0)) / count, floor)


def beyond(difference, error):
    return float(numpy.mean(numpy.abs(difference) > BIAS_ERRORS * error))


def frame_bias(reference, runs, candidate):
    count = block_sums(numpy.ones(reference.shape[:2]))
    floor = FLOOR * numpy.abs(block_sums(reference @ LIGHT)) / count
    off = [(run - reference) @ LIGHT for run in runs]
    total = sum(off)
    squares = sum(one * one for one in off)
    theirs = None if candidate is None else block_sums((candidate - reference) @ LIGHT) / count
    own, share = 0.0, 0.0
    for one in off:
        rest = total - one
        error = block_error((squares - one * one - rest * rest / 3) / 2, count, floor)
        own += beyond(block_sums(one) / count, error) / len(off)
        share += 0 if theirs is None else beyond(theirs, error) / len(off)
    if theirs is None:
        return own, None, None
    error = block_error((squares - total * total / len(off)) / (len(off) - 1), count, floor)
    return own, share, numpy.divide(theirs, error, out=numpy.zeros_like(theirs), where=error > 0)


def map_of(errors, shape):
    strength = numpy.clip(numpy.abs(errors) / 6, 0, 1)[..., None]
    colour = numpy.where((errors > 0)[..., None], [1.0, 0.1, 0.1], [0.1, 0.1, 1.0])
    blocks = 1 - strength + strength * colour
    return numpy.repeat(numpy.repeat(blocks, BLOCK, axis=0), BLOCK, axis=1)[:shape[0], :shape[1]]


def compared(base, track, candidate=None, out=None):
    """The baseline's four RMSEs and its bias share, then the candidate's."""
    names = image_names(track)
    squares = numpy.zeros(len(SEEDS) + 1)
    shares = numpy.zeros(2)
    for name in names:
        reference = read_pfm(base / 'reference' / name)
        runs = [read_pfm(base / ('run-%d' % seed) / name) for seed in SEEDS]
        drawn = None if candidate is None else read_pfm(candidate / name)
        if drawn is not None and drawn.shape != reference.shape:
            refused('%s: not the size of the baseline' % (candidate / name))
        graded = steps(reference)
        squares += [numpy.mean((steps(one) - graded) ** 2) for one in runs + [drawn if drawn is not None else reference]]
        own, share, errors = frame_bias(reference, runs, drawn)
        shares += [own, share or 0]
        if out is not None and errors is not None:
            write_ppm(out / ('difference-' + Path(name).stem + '.ppm'), map_of(errors, reference.shape))
    rmse = numpy.sqrt(squares / len(names))
    return {'rmse': [float(v) for v in rmse[:-1]], 'bias_share': float(shares[0] / len(names)),
            'candidate_rmse': float(rmse[-1]), 'candidate_bias_share': float(shares[1] / len(names))}


# ---------------------------------------------------------------- running

def scene_path(name):
    path = Path(name)
    path = path if path.suffix == '.sdf' else ROOT / 'scenes' / (name + '.sdf')
    if not path.is_file():
        refused('%s: no such scene' % path)
    return path.resolve()


def config():
    with open(CONFIG, 'rb') as file:
        return tomllib.load(file)


def default_binary():
    plugins = config().get('plugins', {})
    name = 'build' + ''.join('-' + plugin.split('/')[1] for plugin in BUILT if not plugins.get(plugin, True))
    return ROOT / 'output' / name / 'kodanuki'


def size_flags(args):
    settings = config()['settings']
    width, height = QUICK if args.quick else (settings['width'], settings['height'])
    found = ['--size', str(width), str(height)]
    for key in NUMBERS:
        found += ['--' + key, str(settings[key])] if key in settings else []
    return found


def image_names(track):
    return ['frame-%04d.pfm' % frame for frame in range(FRAMES)] if track else ['image.pfm']


def cameras(scene):
    found = re.search(r'camera\s*\{[^}]*?eye\s*=\s*\[([^\]]*)\][^}]*?target\s*=\s*\[([^\]]*)\]', scene.read_text(), re.S)
    if not found:
        refused('%s: --track needs a camera block with a plain eye and then a plain target' % scene)
    eye, target = ([float(value) for value in group.split(',')] for group in found.groups())
    lines = []
    for frame in range(FRAMES):
        angle = math.radians(TURN * max(0, frame - FRAMES // 2 + 1))
        x, y = eye[0] - target[0], eye[1] - target[1]
        turned = [target[0] + x * math.cos(angle) - y * math.sin(angle), target[1] + x * math.sin(angle) + y * math.cos(angle), eye[2]]
        lines.append(' '.join('%.9g' % value for value in turned + target))
    return '\n'.join(lines) + '\n'


def clip_scene(scene, cameras_file):
    """A scene beside the cameras that imports the scene and plays them as a clip, one a second."""
    rows = [line.split() for line in Path(cameras_file).read_text().splitlines()]
    eyes = '\n'.join('\t[%d, %s],' % (frame, ', '.join(row[:3])) for frame, row in enumerate(rows))
    targets = '\n'.join('\t[%d, %s],' % (frame, ', '.join(row[3:])) for frame, row in enumerate(rows))
    path = Path(cameras_file).with_name('clip.sdf')
    path.write_text('import "%s";\n\nclip {\n\tduration = %d;\n\trate     = 1;\n}\n\nlet track_eyes = [\n%s\n];\n\nlet track_targets = [\n%s\n];\n\n'
                    'camera {\n\teye    = linear(track_eyes, time);\n\ttarget = linear(track_targets, time);\n}\n' % (scene, len(rows), eyes, targets))
    return path


def drawn(args, out, seed, flags, cameras_file=None):
    """Draw one run into the directory out; return what it cost."""
    out.mkdir(parents=True, exist_ok=True)
    binary = Path(args.binary) if args.binary else default_binary()
    if not binary.is_file():
        refused('%s: no such renderer; build it first (make test builds this one) or name one with --binary' % binary)
    scene = clip_scene(args.scene_file, cameras_file) if cameras_file else args.scene_file
    where = [*PER_FRAME, '--out', out] if cameras_file else ['--out', out / 'image.ppm', '--pfm', out / 'image.pfm']
    command = ['perf', 'stat', '-x,', '-o', out / 'perf.csv', '-e', 'instructions:u', '--', binary, scene,
               *flags, '--threads', args.threads, '--seed', seed, *where]
    start = time.monotonic()
    with open(out / 'log.txt', 'w') as log:
        process = subprocess.Popen([str(word) for word in command], stdout=log, stderr=log)
        _, status, usage = os.wait4(process.pid, 0)
        process.returncode = os.waitstatus_to_exitcode(status)
    wall = time.monotonic() - start
    text = (out / 'log.txt').read_text()
    if process.returncode != 0:
        refused('the renderer failed (exit %d; -9 is the memory cap), see %s\n%s' % (process.returncode, out / 'log.txt', text[-1000:]))
    counted = re.search(r'^(\d+),', (out / 'perf.csv').read_text(), re.M)
    if not counted:
        refused('perf stat counted no instructions, see %s' % (out / 'perf.csv'))
    samples = re.search(r'([\d.]+) samples a pixel', text)
    return {'seed': seed, 'instructions': int(counted.group(1)), 'wall_seconds': round(wall, 2),
            'peak_mb': round(usage.ru_maxrss / 1024, 1), 'samples': float(samples.group(1)) if samples else None}


def mean_of(runs, key):
    values = [run[key] for run in runs if run[key] is not None]
    return sum(values) / len(values) if values else None


def named(args):
    return '%s %s%s' % (args.scene_file.stem, 'quick' if args.quick else 'full', ' track' if args.track else '')


def place(root, args):
    found = Path(root) / args.scene_file.stem / ('quick' if args.quick else 'full')
    return found / 'track' if args.track else found


def read_baseline(args):
    base = place(os.environ.get('KODANUKI_BASELINE', SHARED), args)
    if not (base / 'baseline.json').is_file():
        refused('%s: no baseline; record one with the baseline subcommand' % base)
    return base, json.loads((base / 'baseline.json').read_text())


def commit():
    found = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True)
    return found.stdout.strip()


# ---------------------------------------------------------------- baseline

def track_reference(args, work, flags, cameras_file):
    """The mean of the runs of TRACK_SEEDS, frame by frame; the runs' cost."""
    totals, runs = {}, []
    for seed in TRACK_SEEDS:
        runs.append(drawn(args, work / 'scratch', seed, flags, cameras_file))
        for name in image_names(True):
            totals[name] = totals.get(name, 0) + read_pfm(work / 'scratch' / name)
        print('  reference run %d of %d' % (len(runs), len(TRACK_SEEDS)), flush=True)
    shutil.rmtree(work / 'scratch')
    (work / 'reference').mkdir()
    for name, total in totals.items():
        write_pfm(work / 'reference' / name, total / len(runs))
    return {'seeds': [TRACK_SEEDS[0], TRACK_SEEDS[-1]], 'instructions': sum(run['instructions'] for run in runs),
            'wall_seconds': round(sum(run['wall_seconds'] for run in runs), 2), 'peak_mb': max(run['peak_mb'] for run in runs)}


def baseline(args):
    target = place(os.environ.get('KODANUKI_BASELINE', ROOT / 'output' / 'baseline'), args)
    if (target / 'baseline.json').exists():
        refused('%s: already recorded; remove it to record it again' % target)
    work = target.with_name(target.name + '.partial')
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    flags = size_flags(args) + (TRACK if args.track else [])
    cameras_file = None
    if args.track:
        cameras_file = work / 'track.txt'
        cameras_file.write_text(cameras(args.scene_file))
    runs = []
    for seed in SEEDS:
        runs.append(drawn(args, work / ('run-%d' % seed), seed, flags, cameras_file))
        print('  run %d: %s' % (seed, json.dumps(runs[-1])), flush=True)
    if args.track:
        reference = track_reference(args, work, flags, cameras_file)
    else:
        reference = drawn(args, work / 'reference', REFERENCE_SEED, flags + REFERENCE)
    print('  reference: %s' % json.dumps(reference), flush=True)
    record = {'scene': args.scene_file.stem, 'size': 'quick' if args.quick else 'full', 'track': args.track,
              'commit': commit(), 'flags': flags, 'threads': args.threads,
              'instructions': mean_of(runs, 'instructions'), 'wall_seconds': mean_of(runs, 'wall_seconds'),
              'peak_mb': mean_of(runs, 'peak_mb'), 'samples': mean_of(runs, 'samples'),
              'runs': runs, 'reference': reference, **{key: value for key, value in compared(work, args.track).items() if not key.startswith('candidate')}}
    (work / 'baseline.json').write_text(json.dumps(record, indent='\t') + '\n')
    target.mkdir(exist_ok=True)
    for entry in work.iterdir():
        entry.rename(target / entry.name)
    work.rmdir()
    print('%s: instructions %.4g, wall %.1f s, peak %.0f MB, samples a pixel %s, rmse %s, bias share %.2f%%' % (
        named(args), record['instructions'], record['wall_seconds'], record['peak_mb'], record['samples'],
        ' '.join('%.4f' % value for value in record['rmse']), 100 * record['bias_share']))
    print('wrote %s' % target)


# ---------------------------------------------------------------- check

def verdict(passed):
    return 'pass' if passed else 'FAIL'


def cost_line(run, record):
    return 'instructions %.4g (%.3fx), peak %.0f MB (%.3fx), wall %.1f s' % (
        run['instructions'], run['instructions'] / record['instructions'], run['peak_mb'], run['peak_mb'] / record['peak_mb'], run['wall_seconds'])


def candidate_of(args, record, out):
    if args.pfm:
        if args.track or args.samples is None:
            refused('--pfm: one still image, and --samples with its mean samples a pixel')
        shutil.copyfile(args.pfm, out / 'image.pfm')
        return {'samples': args.samples}
    uses = [word for use in args.use for word in ('--use', use)]
    return drawn(args, out, args.seed, record['flags'] + uses, out.parent / 'track.txt' if args.track else None)


def check(args):
    base, record = read_baseline(args)
    args.threads = args.threads or record['threads']
    out = place(ROOT / 'output' / 'check', args) / 'candidate'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    if args.track:
        shutil.copyfile(base / 'track.txt', out.parent / 'track.txt')
    run = candidate_of(args, record, out)
    found = compared(base, args.track, out, out.parent)
    noise_limit = NOISE_LIMIT * max(found['rmse'])
    bias_limit = found['bias_share'] + BIAS_LIMIT
    passed = [found['candidate_rmse'] <= noise_limit, found['candidate_bias_share'] <= bias_limit]
    print('%s, candidate %s' % (named(args), args.pfm or 'seed %d %s' % (args.seed, ' '.join(args.use))))
    print('noise   rmse %.4f steps, baseline %s, limit %.4f: %s' % (
        found['candidate_rmse'], ' '.join('%.4f' % value for value in found['rmse']), noise_limit, verdict(passed[0])))
    print('bias    %.2f%% of blocks beyond %d standard errors, baseline %.2f%%, limit %.2f%%: %s' % (
        100 * found['candidate_bias_share'], BIAS_ERRORS, 100 * found['bias_share'], 100 * bias_limit, verdict(passed[1])))
    if args.track:
        print('effort  not judged, every frame draws one sample a pixel')
    else:
        ratio = run['samples'] / record['samples']
        passed.append(abs(ratio - 1) <= EFFORT_LIMIT)
        print('effort  %.1f samples a pixel, baseline %.1f, ratio %.3f, limit %.2f to %.2f: %s' % (
            run['samples'], record['samples'], ratio, 1 - EFFORT_LIMIT, 1 + EFFORT_LIMIT, verdict(passed[2])))
    if 'instructions' in run:
        print('cost    %s (not judged here)' % cost_line(run, record))
    print('map     %s' % (out.parent / 'difference-*.ppm'))
    (out.parent / 'check.json').write_text(json.dumps({'run': run, **found, 'passed': passed}, indent='\t') + '\n')
    print('within noise' if all(passed) else 'NOT within noise')
    raise SystemExit(0 if all(passed) else 1)


# ---------------------------------------------------------------- budget

def report_lines(report):
    """The seconds by step and plugin, and the counters, of a metrics report."""
    steps, counters = {}, {}
    for row in report['steps']:
        key = (row['step'], row['plugin'], 'frame' if row['worker'] < 0 else 'workers')
        steps[key] = steps.get(key, 0) + row['seconds']
    for row in report['counters']:
        key = (row['plugin'], row['name'])
        counters[key] = row['value'] if row['plugin'] == 'core' else counters.get(key, 0) + row['value']
    lines = ['report  %s, %d frames, peak %.0f MB' % (report['scene'], report['frames'], report['peak_mb'])]
    lines += ['  %-16s %-20s %-7s %9.3f s' % (step, plugin, where, seconds) for (step, plugin, where), seconds in steps.items()]
    lines += ['  %-20s %-16s %.6g' % (plugin, name, value) for (plugin, name), value in counters.items()]
    return lines


def report_of(out, args):
    found = out / (args.scene_file.stem + '.metrics.json')
    return json.loads(found.read_text()) if found.is_file() else None


def budget(args):
    if args.track:
        refused('budget: the still only')
    _, record = read_baseline(args)
    args.threads = args.threads or record['threads']
    if args.last:
        out = place(ROOT / 'output' / 'check', args) / 'candidate'
        last = out.parent / 'check.json'
        run = json.loads(last.read_text())['run'] if last.is_file() else {}
        if 'instructions' not in run:
            refused('%s: the last check drew no run' % last)
    else:
        out = place(ROOT / 'output' / 'check', args) / 'budget'
        shutil.rmtree(out, ignore_errors=True)
        uses = [word for use in args.use for word in ('--use', use)]
        run = drawn(args, out, SEEDS[0], record['flags'] + uses)
    passed = run['instructions'] <= BUDGET_LIMIT * record['instructions'] and run['peak_mb'] <= BUDGET_LIMIT * record['peak_mb']
    print('%s: %s, limit %.1fx: %s' % (named(args), cost_line(run, record), BUDGET_LIMIT, verdict(passed)))
    report = report_of(out, args)
    if report:
        print('\n'.join(report_lines(report)))
    raise SystemExit(0 if passed else 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', choices=('baseline', 'check', 'budget'))
    parser.add_argument('scene')
    parser.add_argument('--quick', action='store_true', help='480 x 300 rather than the full size')
    parser.add_argument('--track', action='store_true', help='the clip of 8 frames rather than the still (baseline, check)')
    parser.add_argument('--use', action='append', default=[], help='a --use key=value for the candidate (check) or the run (budget)')
    parser.add_argument('--seed', type=int, default=CANDIDATE_SEED, help='the seed of the candidate (check), default 5')
    parser.add_argument('--pfm', help='a candidate drawn elsewhere (check); needs --samples')
    parser.add_argument('--samples', type=float, help='the mean samples a pixel of --pfm')
    parser.add_argument('--last', action='store_true', help='judge the run of the last check rather than draw (budget)')
    parser.add_argument('--threads', type=int, help='default 5 (baseline) or the baseline\'s (check, budget)')
    parser.add_argument('--binary', help='the renderer, default the build of this worktree')
    args = parser.parse_args()
    args.scene_file = scene_path(args.scene)
    if args.command == 'baseline':
        args.threads = args.threads or THREADS
    {'baseline': baseline, 'check': check, 'budget': budget}[args.command](args)


main()
