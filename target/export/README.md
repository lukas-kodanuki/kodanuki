# Kodanuki

A spectral path tracer for scenes written as signed distance fields
(`.sdf`). It needs Linux on an x86-64 with AVX2, `python3` 3.11 or
newer and `make`; the viewer needs a window.

Set the scene and the config in the `Makefile`, then:

```
make render   draw the scene to output/<scene>.ppm (graded) and .pfm (linear)
make viewer   fly through the scene in a window
make clean    delete output/
```

`target/configs/render_final.toml` draws final frames without the biased
extras, `render_fast.toml` quick ones with them; `make viewer CONFIG=target/configs/viewer_fast.toml` flies through the
scene with them. A config's `[extras]` lists every extra, `true` or `false`. In the
viewer W, A, S, D move, Space and Ctrl rise and sink, Shift is faster,
the right mouse button turns the view, the wheel scales the speed, R
loads the scene file again and Escape quits.
