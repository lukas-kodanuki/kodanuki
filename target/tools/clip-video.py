#!/usr/bin/env -S python3 -B
import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

USAGE = 'clip-video.py directory [--scene scene.sdf | --rate frames-a-second]: joins frame-0000.ppm onward into clip.mp4 beside them'


def refused(message):
    print(message, file=sys.stderr)
    raise SystemExit(1)


def scene_rate(scene):
    clip = re.search(r'\bclip\s*\{([^{}]*)\}', Path(scene).read_text())
    rate = clip and re.search(r'\brate\s*=\s*([0-9.]+)\s*;', clip.group(1))
    if not rate:
        refused('%s: no clip block with a plain rate; give --rate' % scene)
    return float(rate.group(1))


def main():
    parser = argparse.ArgumentParser(usage=USAGE)
    parser.add_argument('directory')
    parser.add_argument('--scene')
    parser.add_argument('--rate', type=float, default=24)
    args = parser.parse_args()
    directory = Path(args.directory)
    if not (directory / 'frame-0000.ppm').is_file():
        refused('%s: no frame-0000.ppm' % directory)
    if not shutil.which('ffmpeg'):
        refused('ffmpeg is not installed')
    rate = scene_rate(args.scene) if args.scene else args.rate
    command = ['ffmpeg', '-y', '-loglevel', 'error', '-framerate', '%g' % rate, '-i', 'frame-%04d.ppm',
               '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '18', 'clip.mp4']
    raise SystemExit(subprocess.run(command, cwd=directory).returncode)


main()
