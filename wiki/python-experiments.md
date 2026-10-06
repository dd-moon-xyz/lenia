---
type: Documentation
title: Python Experiments
---

# Python Experiments

Before moving everything into CUDA, I used [resources/fft_numba.py](../resources/fft_numba.py) to prototype the FFT pipeline and generate quick Matplotlib-based debug animations. That was substantially easier to inspect than debugging the same ideas inside CUDA kernels, and it helped validate padding, FFT multiplication, inverse transforms, and shift behavior before porting them to the GPU implementation. Below you can see an animation showing an instance of $\textit{Orbium Unicaudatus}$ evolving under the FFT-based update rule, and interacting with a wall obstacle. The top-right corner shows $|\hat{G}|$, the bottom left shows the shifted convolution result $G_{\mathrm{shift}}$, and the bottom-right corner shows just the growth function $H$ multiplied by $\Delta t$.

<p align="center">
<img src="../resources/videos/wall.gif" alt="FFT prototype wall experiment" width="50%" />
</p>

[Wiki index](index.md)
