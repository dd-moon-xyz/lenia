---
type: Documentation
title: Controls
---

# Controls

The current application is keyboard-and-mouse driven and built around immediate visual feedback.

- `Left` / `Right` in normal mode: switch to the previous or next animal
- `C`: toggle control mode
- `Left` / `Right` in control mode: steer the current animal by injecting "zero"-mass orthogonal to its current movement vector
- `Up` / `Down`: change simulation scale (thanks to FFT, this is O(n log n), and not O(n^2), like in a traditional convolution)
- `R`: reset the current organism
- `P`: pause or resume
- `D`: circular draw mode
- `Q`: stencil placement mode using the selected animal
- Mouse wheel in draw mode: adjust draw radius
- Left mouse button: add cells
- Right mouse button: erase cells in circle mode
- `I`: show runtime stats and kernel previews
- `B`, `G`, `M`: toggle bounding boxes, grid, and center-of-mass overlays
- `X`: Shader Control Panel

The steering behavior is the distinctive part. The simulation continuously tracks center of mass and heading, then uses that direction vector to place perturbations to the left or right of the animal. This doesn't work for all animals, but for those with a clear, linear motion, the effect is quite seamless.

[Wiki index](index.md)
