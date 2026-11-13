#!/usr/bin/env -S python3 -B
import argparse
import sys
from pathlib import Path

USAGE = 'track-stops.py track.txt [--rate frames-a-second]: an old camera track as two lists of stops, eyes and targets, to paste into a scene'


def refused(message):
    print(message, file=sys.stderr)
    raise SystemExit(1)


def cameras(path, rate):
    found = []
    for number, line in enumerate(Path(path).read_text().splitlines(), 1):
        values = [float(word) for word in line.split()]
        if len(values) not in (6, 7):
            refused('%s:%d: a track line holds six numbers, the eye and then the target, or a seventh, the time' % (path, number))
        time = values[6] if len(values) == 7 else (number - 1) / rate
        found.append((time, values[0:3], values[3:6]))
    if not found:
        refused('%s: a track holds at least one camera' % path)
    return found


def number(value):
    return ('%.6f' % value).rstrip('0').rstrip('.') or '0'


def stops(name, rows):
    lines = ['\t[%s],' % ', '.join(number(value) for value in [time, *values]) for time, values in rows]
    return 'let %s = [\n%s\n];\n' % (name, '\n'.join(lines))


def main():
    parser = argparse.ArgumentParser(usage=USAGE)
    parser.add_argument('track')
    parser.add_argument('--rate', type=float, default=24)
    args = parser.parse_args()
    found = cameras(args.track, args.rate)
    print(stops('eyes', [(time, eye) for time, eye, _ in found]))
    print(stops('targets', [(time, target) for time, _, target in found]))
    print('camera {\n\teye    = linear(eyes, time);\n\ttarget = linear(targets, time);\n}')


main()
