#!/usr/bin/env python3
import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import numpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CONFIGURATIONS = [
    ('mean x1', 'mean', 1),
    ('temporal x1', 'temporal', 1),
    ('temporal+denoise x1', 'temporal+denoise', 1),
    ('temporal+denoise+upscale x2', 'temporal+denoise+upscale', 2),
    ('temporal+denoise+upscale x4', 'temporal+denoise+upscale', 4),
]
FRAMES = 32
STILL_FRAMES = [1, 2, 4, 8, 16, 32]
MOVING_STEPS = 24
MOVING_GROUPS = [(1, 8), (9, 24), (25, 32)]
CHECKED_CAMERA = MOVING_STEPS - 1
CHECK_SEED = 7919
COLUMNS = ['config', 'track', 'frame', 'seconds', 'relMSE', 'PSNR', 'perceptual', 'flicker', 'estimated']
METRICS = '''metrics, per frame against the reference of the frame's camera:
  relMSE      mean over pixels and channels of (image - reference)^2 / (reference^2 + 0.01), linear light
  PSNR        dB, of the graded images (filmic, gamma 2.2) at 8 bits
  perceptual  mean CIE76 delta E between the graded images in CIELAB (D65), each blurred by a 5x5 box first
  flicker     mean |(graded frame - graded frame before) - (graded reference - graded reference before)|, in 8-bit steps; none on frame 1
  estimated   the renderer's no-reference noise of the temporal history (its "noise" printout): the mean over pixels of
              the relative variance of the pixel's blend, (moment - lum^2) / ((length - 1)(lum^2 + 0.01)); a pixel of
              fewer than 2 samples counts one sample's relative variance (the mean over the others), 1 where none has 2'''

def read_linear(path):
    with open(path, 'rb') as f:
        data = f.read()
    _, size, scale, body = data.split(b'\n', 3)
    width, height = map(int, size.split())
    pixels = numpy.frombuffer(body[:width * height * 12], dtype='<f4' if float(scale) < 0 else '>f4')
    return pixels.reshape(height, width, 3)[::-1].astype(numpy.float64)

def relative_mse(image, reference):
    return float(numpy.mean((image - reference) ** 2 / (reference ** 2 + 0.01)))

def psnr(a, b):
    a8 = numpy.round(a * 255)
    b8 = numpy.round(b * 255)
    mse = numpy.mean((a8 - b8) ** 2)
    return float(10 * numpy.log10(255 ** 2 / mse)) if mse > 0 else float('inf')

def scene_camera(text):
    block = re.search(r'camera\s*\{(.*?)\}', text, re.S).group(1)
    eye = [float(v) for v in re.search(r'eye\s*=\s*\[([^\]]*)\]', block).group(1).split(',')]
    target = [float(v) for v in re.search(r'target\s*=\s*\[([^\]]*)\]', block).group(1).split(',')]
    return numpy.array(eye), numpy.array(target)

def placed_scene(text, directory, camera):
    absolute = re.sub(r'import\s+"([^"/][^"]*)"', lambda m: 'import "%s"' % os.path.join(directory, m.group(1)), text)
    eye, target = camera

    def moved(match):
        block = re.sub(r'eye\s*=\s*\[[^\]]*\]', 'eye = [%.9g, %.9g, %.9g]' % tuple(eye), match.group(0))
        return re.sub(r'target\s*=\s*\[[^\]]*\]', 'target = [%.9g, %.9g, %.9g]' % tuple(target), block)

    return re.sub(r'camera\s*\{.*?\}', moved, absolute, count=1, flags=re.S)

def camera_index(frame):
    return min(frame - 1, MOVING_STEPS - 1)

def sideways_track(camera, share):
    eye, target = camera
    forward = target - eye
    right = numpy.cross(forward, numpy.array([0.0, 0.0, 1.0]))
    right /= numpy.linalg.norm(right)
    reach = share * numpy.linalg.norm(forward)
    cameras = []
    for frame in range(1, FRAMES + 1):
        offset = right * (reach * camera_index(frame) / (MOVING_STEPS - 1))
        cameras.append((eye + offset, target + offset))
    return cameras

def write_track(path, cameras):
    with open(path, 'w') as f:
        for eye, target in cameras:
            f.write('%.9g %.9g %.9g %.9g %.9g %.9g\n' % (*eye, *target))

def memory_left():
    with open('/proc/meminfo') as f:
        fields = dict((line.split(':')[0], int(line.split()[1])) for line in f)
    return fields['MemAvailable'] / 2 ** 20, fields['SwapFree'] / 2 ** 20

def run(options, arguments):
    available, swap = memory_left()
    if available < 1.5 or swap < 4:
        sys.exit('stopped: %.1f GB available, %.1f GB swap free' % (available, swap))
    command = shlex.split(options.launcher) + arguments
    print(' '.join(command), flush=True)
    done = subprocess.run(command, stderr=subprocess.PIPE, text=True)
    if done.returncode != 0:
        sys.stderr.write(done.stderr)
        sys.exit(1)
    return done.stderr

def common_flags(options):
    return ['--size', str(options.width), str(options.height), '--threads', str(options.threads)]

def rendered_reference(options, scene_path, path, extra):
    if not os.path.exists(path):
        run(options, [options.binary, scene_path, *common_flags(options), '--use', 'stopping=fixed', *extra, '--out', path[:-4] + '.ppm', '--pfm', path])
    image = read_linear(path)
    return image.astype(numpy.float32), compare.graded(image).astype(numpy.float32)

def placed_reference(options, cameras, index, seed):
    directory = os.path.join(options.directory, 'refs')
    placed = os.path.join(directory, 'camera-%02d.sdf' % index)
    with open(placed, 'w') as f:
        f.write(placed_scene(options.text, os.path.dirname(options.scene), cameras[index]))
    extra = ['--samples', str(options.samples)] + (['--seed', str(seed)] if seed is not None else [])
    name = 'camera-%02d%s-%d.pfm' % (index, '' if seed is None else '-seed%d' % seed, options.samples)
    return rendered_reference(options, placed, os.path.join(directory, name), extra)

def frame_log(log):
    lines = re.findall(r'frame (\d+), ([0-9.]+) s:[^\n\r]*?(?:, noise ([0-9.eE+-]+))?(?=[\r\n]|$)', log)
    return {int(frame) + 1: (float(seconds), float(noise) if noise else None) for frame, seconds, noise in lines}

def frame_rows(options, case, references, log):
    name, track = case
    rows = []
    before = None
    for frame in range(1, FRAMES + 1):
        path = os.path.join(options.frames, 'frame-%04d.pfm' % (frame - 1))
        image = read_linear(path)
        shown = compare.graded(image)
        reference, reference_shown = references[frame]
        flicker = None
        if before is not None:
            flicker = float(numpy.mean(numpy.abs((shown - before[0]) - (reference_shown - before[1])))) * 255
        seconds, noise = log[frame]
        rows.append({
            'config': name,
            'track': track,
            'frame': frame,
            'seconds': seconds,
            'relMSE': relative_mse(image, reference),
            'PSNR': psnr(shown, reference_shown),
            'perceptual': compare.perceptual_error(shown, reference_shown),
            'flicker': flicker,
            'estimated': noise,
        })
        before = (shown, reference_shown)
    return rows

def measured_track(options, track, configuration, references):
    name, reconstruction, factor = configuration
    options.frames = os.path.join(options.directory, 'frames')
    shutil.rmtree(options.frames, ignore_errors=True)
    track_path = os.path.join(options.directory, track + '.txt')
    log = run(options, [options.binary, options.scene, *common_flags(options), '--frames', str(FRAMES), '--track', track_path, '--out', options.frames, '--reconstruction', reconstruction, '--factor', str(factor)])
    rows = frame_rows(options, (name, track), references, frame_log(log))
    shutil.rmtree(options.frames)
    return rows

def cell(value):
    if value is None:
        return ''
    if isinstance(value, float):
        return '%.6g' % value
    return str(value)

def write_csv(path, rows):
    with open(path, 'w') as f:
        f.write(','.join(COLUMNS) + '\n')
        for row in rows:
            f.write(','.join(cell(row[c]) for c in COLUMNS) + '\n')

def averaged(rows, key):
    values = [row[key] for row in rows if row[key] is not None]
    return sum(values) / len(values) if values else float('nan')

def group_cells(rows):
    return '  %9.5f %6.2f %5.2f %5.2f' % tuple(averaged(rows, key) for key in ['relMSE', 'PSNR', 'perceptual', 'flicker'])

def frame_seconds(rows):
    return averaged([row for row in rows if row['frame'] > 1], 'seconds')

def table(title, rows, groups):
    labels = ['frame %d' % low if low == high else 'frames %d-%d' % (low, high) for low, high in groups]
    lines = [title, '%-21s' % 'configuration' + ''.join('  %-28s' % label for label in labels) + '  s/frame']
    lines.append('%-21s' % '' + ''.join('  %-28s' % '   relMSE   PSNR    dE  flkr' for _ in groups))
    names = [c[0] for c in CONFIGURATIONS if any(row['config'] == c[0] for row in rows)]
    for name in names:
        mine = [row for row in rows if row['config'] == name]
        cells = ''.join(group_cells([row for row in mine if low <= row['frame'] <= high]) for low, high in groups)
        lines.append('%-21s%s  %7.3f' % (name, cells, frame_seconds(mine)))
    return '\n'.join(lines)

def ranks(values):
    order = numpy.argsort(values, kind='stable')
    ranked = numpy.empty(len(values))
    ranked[order] = numpy.arange(len(values))
    for value in numpy.unique(values):
        same = values == value
        ranked[same] = ranked[same].mean()
    return ranked

def correlations(rows):
    estimated = numpy.array([row['estimated'] for row in rows])
    real = numpy.array([row['relMSE'] for row in rows])
    pearson = numpy.corrcoef(estimated, real)[0, 1]
    logarithmic = numpy.corrcoef(numpy.log(estimated + 1e-6), numpy.log(real))[0, 1]
    spearman = numpy.corrcoef(ranks(estimated), ranks(real))[0, 1]
    return len(rows), pearson, logarithmic, spearman

def correlation_table(rows):
    estimated = [row for row in rows if row['estimated'] is not None]
    groups = [
        ('all temporal', estimated),
        ('all, frames 2-32', [row for row in estimated if row['frame'] > 1]),
        ('temporal', [row for row in estimated if row['config'].startswith('temporal x')]),
        ('temporal+denoise', [row for row in estimated if row['config'].startswith('temporal+denoise x')]),
        ('temporal+denoise+upscale', [row for row in estimated if row['config'].startswith('temporal+denoise+upscale')]),
        ('still track', [row for row in estimated if row['track'] == 'still']),
        ('moving track', [row for row in estimated if row['track'] == 'moving']),
    ]
    lines = ['estimated noise against relMSE', '%-21s  %5s  %8s  %12s  %8s' % ('rows', 'n', 'Pearson', 'Pearson log', 'Spearman')]
    for name, chosen in groups:
        if len(chosen) > 2:
            lines.append('%-21s  %5d  %8.3f  %12.3f  %8.3f' % ((name,) + correlations(chosen)))
    return '\n'.join(lines)

def reference_check(options, cameras, references):
    image, shown = placed_reference(options, cameras, CHECKED_CAMERA, CHECK_SEED)
    reference, reference_shown = references[CHECKED_CAMERA + 1]
    between = relative_mse(image.astype(numpy.float64), reference.astype(numpy.float64))
    return 'reference noise (camera %d, two seeds of %d samples): relMSE of one reference %.6f (half the pair\'s), PSNR between them %.2f dB, perceptual %.3f' % (
        CHECKED_CAMERA, options.samples, between / 2, psnr(shown, reference_shown), compare.perceptual_error(shown, reference_shown))

def moving_references(options, cameras):
    placed = {index: placed_reference(options, cameras, index, None) for index in range(MOVING_STEPS)}
    return {frame: placed[camera_index(frame)] for frame in range(1, FRAMES + 1)}

def measured_scene(options):
    options.scene = os.path.abspath(options.scene)
    name = os.path.splitext(os.path.basename(options.scene))[0]
    options.directory = os.path.join(ROOT, 'output', 'quality', name)
    os.makedirs(options.directory, exist_ok=True)
    with open(options.scene) as f:
        options.text = f.read()
    camera = scene_camera(options.text)
    write_track(os.path.join(options.directory, 'still.txt'), [camera])
    cameras = sideways_track(camera, options.move)
    write_track(os.path.join(options.directory, 'moving.txt'), cameras)
    os.makedirs(os.path.join(options.directory, 'refs'), exist_ok=True)
    still = rendered_reference(options, options.scene, os.path.join(options.directory, 'refs', 'still-%d.pfm' % options.still_samples), ['--samples', str(options.still_samples)])
    moving = moving_references(options, cameras)
    check = reference_check(options, cameras, moving)
    chosen = [c for c in CONFIGURATIONS if c[1] in options.reconstructions]
    rows = []
    for configuration in chosen:
        rows += measured_track(options, 'still', configuration, {frame: still for frame in range(1, FRAMES + 1)})
        rows += measured_track(options, 'moving', configuration, moving)
    write_csv(os.path.join(options.directory, 'frames.csv'), rows)
    heading = '%s at %dx%d, references of %d samples (fixed) for every moving camera, %d for the still one; moving: %d frames sideways by %.0f%% of the view distance, then %d still' % (
        name, options.width, options.height, options.samples, options.still_samples, MOVING_STEPS, options.move * 100, FRAMES - MOVING_STEPS)
    still_table = table('still (single frames)', [row for row in rows if row['track'] == 'still'], [(f, f) for f in STILL_FRAMES])
    moving_table = table('moving (means over frames)', [row for row in rows if row['track'] == 'moving'], MOVING_GROUPS)
    summary = '\n\n'.join([heading, METRICS, check, still_table, moving_table, correlation_table(rows)])
    with open(os.path.join(options.directory, 'summary.txt'), 'w') as f:
        f.write(summary + '\n')
    print(summary)

def parsed_options():
    parser = argparse.ArgumentParser(description='real-time image quality of the track reconstructions against offline references, per frame')
    parser.add_argument('scene')
    parser.add_argument('--binary', default=os.path.join(ROOT, 'output', 'build', 'kodanuki'))
    parser.add_argument('--size', nargs=2, type=int, default=[480, 300], metavar=('W', 'H'))
    parser.add_argument('--samples', type=int, default=512)
    parser.add_argument('--still-samples', type=int, default=1024)
    parser.add_argument('--threads', type=int, default=6)
    parser.add_argument('--move', type=float, default=0.06)
    parser.add_argument('--reconstructions', default='mean,temporal,temporal+denoise,temporal+denoise+upscale')
    parser.add_argument('--launcher', default='', help='a command every render runs under, as "flock lock nice"')
    options = parser.parse_args()
    options.width, options.height = options.size
    options.reconstructions = options.reconstructions.split(',')
    return options

if __name__ == '__main__':
    measured_scene(parsed_options())
