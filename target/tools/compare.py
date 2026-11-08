#!/usr/bin/env python3
import re
import sys
import numpy

def read_image(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data.startswith(b'P6'):
        header = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', data)
        width, height, top = (int(value) for value in header.groups())
        pixels = numpy.frombuffer(data[header.end():header.end() + width * height * 3], dtype=numpy.uint8)
        return pixels.reshape(height, width, 3).astype(numpy.float64) / top
    _, size, scale, body = data.split(b'\n', 3)
    width, height = map(int, size.split())
    pixels = numpy.frombuffer(body[:width * height * 12], dtype='<f4' if float(scale) < 0 else '>f4')
    image = pixels.reshape(height, width, 3)[::-1].astype(numpy.float64)
    return graded(image)

def filmic(value):
    v = numpy.maximum(value, 0)
    return numpy.where(v >= 16, 2.51 / 2.43, v * (2.51 * v + 0.03) / (v * (2.43 * v + 0.59) + 0.14))

def linear(display):
    target = numpy.clip(display, 0, 1) ** 2.2
    lower = numpy.zeros_like(target)
    upper = numpy.full_like(target, 16.0)
    for _ in range(40):
        middle = (lower + upper) / 2
        below = filmic(middle) < target
        lower = numpy.where(below, middle, lower)
        upper = numpy.where(below, upper, middle)
    return (lower + upper) / 2

def blurred(image):
    padded = numpy.pad(image, ((2, 2), (2, 2), (0, 0)), mode='edge')
    total = numpy.zeros_like(image)
    for dy in range(5):
        for dx in range(5):
            total += padded[dy:dy + image.shape[0], dx:dx + image.shape[1]]
    return total / 25

def graded(image):
    return numpy.clip(filmic(image), 0, 1) ** (1 / 2.2)

def lab(display):
    light = numpy.clip(display, 0, 1) ** 2.2
    xyz = light @ numpy.array([[0.4124, 0.2126, 0.0193], [0.3576, 0.7152, 0.1192], [0.1805, 0.0722, 0.9505]])
    ratio = xyz / numpy.array([0.9505, 1.0, 1.089])
    f = numpy.where(ratio > (6 / 29) ** 3, numpy.cbrt(ratio), ratio / (3 * (6 / 29) ** 2) + 4 / 29)
    return numpy.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], axis=-1)

def perceptual_error(a, b):
    return float(numpy.mean(numpy.linalg.norm(blurred(lab(a)) - blurred(lab(b)), axis=-1)))

def report(a, b):
    a8 = numpy.round(a * 255)
    b8 = numpy.round(b * 255)
    mse = numpy.mean((a8 - b8) ** 2)
    psnr = 10 * numpy.log10(255 ** 2 / mse) if mse > 0 else float('inf')
    la, lb = linear(a), linear(b)
    relative = numpy.mean(numpy.abs(la - lb) / (lb + 0.01))
    blur = numpy.mean(numpy.abs(blurred(a8) - blurred(b8)))
    bias = numpy.mean(la) / numpy.mean(lb) - 1
    print('PSNR %.2f dB  mean |diff| %.2f steps  blurred |diff| %.2f steps  mean relative error %.4f  mean linear ratio %+.4f' % (
        psnr, numpy.mean(numpy.abs(a8 - b8)), blur, relative, bias))

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('usage: compare.py image.ppm reference.ppm')
        sys.exit(1)
    report(read_image(sys.argv[1]), read_image(sys.argv[2]))
