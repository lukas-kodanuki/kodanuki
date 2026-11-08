# Geometry

How rays find surfaces. The tree (patches, program cells, level of
detail, compression; reference_architecture.md) takes its exact faces from the
derivation below.

## Aims

- Nothing at render time iterates toward the surface. A ray descends
  a tree and, near the surface, solves the cell's functions exactly in
  closed form; a hit evaluates the SDF once (normal, primitive, seam
  share).
- Uniform: it works from the programs alone, with no hints from the
  scene file, and nothing is guessed. Every approximation is proven
  within the tolerance.
- A cell holds several functions, so edges and corners don't force
  subdivision.

## The pieces

1. **One program per object.** The CSG of an object compiles into one
   straight-line program. A leaf is inlined with its placement as
   steps, hard joins become `min`/`max`, and smooth joins one smooth
   minimum step. Every step carries the leaf it came from, for materials.
   The smooth minimum is not a crease: its value and gradient are
   continuous, so its Hessian bounds (the hull of both sides across the
   band edge) still prove a Taylor quadric.
2. **Four number types, one interpreter.** `sdf/program` evaluates a
   program as point (double), dual (value and gradient), interval, or
   hyperdual (value, gradient and Hessian).
3. **Cutting a program to a cell.** An interval evaluation over the
   cell resolves every crease (`min`, `max`, `abs`, comparison,
   `select`) whose sides are separated, and drops the dead steps.
   Children cut their parent's cut program, so programs shrink down the
   tree. A cell whose interval excludes 0 is empty or filled.
4. **Creases left are the faces.** The remaining creases make the
   program piecewise smooth. Every combination of crease sides is a
   smooth piece. A cell's faces are the distinct smooth functions whose
   sign can change in it: the pieces and the crease conditions `a - b`.
   The truth table is the program's decision over those signs. Faces
   the table doesn't depend on are dropped, counting only sign patterns
   a point of the cell can have: parallel exact planes order their signs
   along their normal, so stacked slabs leave the one face that shows. A clamp squared,
   `max(x, 0)²` (rounded boxes, lengths of clamped vectors), is C1 and
   so one positive-square step, not a crease; it is split like one only
   where a square root over it may reach 0 (sharp edges).
5. **A face is a quadric.** A piece affine in one square root,
   `a sqrt(S) + b` with `-b/a > 0` on the cell, has the sign of
   `S - (b/a)^2`; that is the face (exact for spheres, cylinders, cones,
   capsules and rounded corners), held to the tolerance times
   `(-b/a)/|a|`. A hyperdual evaluation at the cell centre
   gives value, gradient and Hessian: the 10 coefficients of a Taylor
   quadric, in the cell's unit coordinates. An interval evaluation of
   (piece minus quadric) over the cell proves the error within the
   tolerance, or the cell is split.
   A piece that is `a sqrt(P^2 + Z^2) + b` with `P = c sqrt(U^2 + V^2) + d`,
   for affine `U`, `V`, `Z` and constants with `R = -d/c > r = -b/(a |c|)
   > 0`, is a torus under some affine map, whatever the placement or
   stretch: it is kept as a ring face (the map, `R` and `r`), exact at
   any size, where its Taylor quadric is not proven. The walk solves
   its quartic `(|v|^2 + R^2 - r^2)^2 - 4 R^2 (v_x^2 + v_y^2)`.
6. **Leaf or split.** The tree branches 4 x 4 x 4 (a 64-tree). A cell
   is a leaf when every face is proven, it has at most 8 faces, and
   either every face is proven within 1% of the tolerance (exact fits
   stop whatever the cell's size) or a cost model says solving its k
   faces per ray is cheaper than descending into its children
   (estimated from their face counts one level down). At the floor (the
   depth limit or a cell of 4 tolerances) a cell of more than 8 faces
   first merges a face into another of the same look whose quadric is
   proven within the tolerance of it; what is left over 8 faces, or
   unproven, becomes an unproven floor leaf.
   The tolerance is half a pixel's footprint, at least 1e-4 of the
   region; a cell wholly outside the frame (and 3 pixels around it) is
   held to 4 times that, as are the offsets of rays leaving it.
7. **Materials are edges.** Faces of different looks (material and
   coats) are never one face, so where two paints meet the table
   decides which face the ray crosses. Inside a smooth join of two
   looks the cell splits on `a - b`: the blend face is gathered twice,
   painted by each operand on its side.
8. **Render.** Descend the 64-tree, skipping empty children by their
   occupancy bits. In a surface leaf, solve each face's quadratic along
   the ray inside the cell, and walk the roots in order until the truth
   table turns inside (the signs before the first root are read in the
   widest gap between roots and unwound, so a root just before the
   cell's entry, within the slack, is not counted twice); of roots
   within the slack, the first face that turns it wins (a tie of
   coplanar faces goes to the union's left operand). The normal is the
   face quadric's gradient. A hit returns the leaf of its face and that
   leaf's material. Rays are not cut at `far`.
   A ray from inside crosses roots within the slack of one another
   together. It leaves where they turn the table outside, and it also
   stops where they keep it inside but the solid just past them, read
   from the leaf's programs, is of another material than the one it
   travels in: where a glass rests on a floor, the ray meets the floor.

## Seams for later

- Other fits behind the face seam (least squares, higher order).
- Coarser approximations (a face's plane part) for far cells.
- The cost model's weights.
