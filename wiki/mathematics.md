---
type: Documentation
title: Mathematical Background
---

# Mathematical Background

Lenia’s simulation can be expressed as a series of transformations on the continuous lattice:

## 1. Forward FFT of the field

The spatial field is transformed into frequency space.

$$
\hat{F}_t(k_x,k_y) =
\mathcal{F}\left\lbrace F_t(x,y)\right\rbrace
$$

where $F_t$ is the spatial field at time $t$, $\mathcal{F}$ is the [2D discrete Fourier transform](https://en.wikipedia.org/wiki/Discrete_Fourier_transform#Two-dimensional_DFT), and $\hat{F}_t$ is the resulting frequency-domain representation of the field at time $t$.

## 2. Convolution in frequency domain

Using the [convolution theorem](https://en.wikipedia.org/wiki/Convolution_theorem), convolution becomes elementwise multiplication.

$$

\begin{align}

\hat{K}(k_x,k_y) &= \mathcal{F}\left\lbrace K(x,y)\right\rbrace \\

\hat{G}(k_x,k_y) &=
\hat{F}_t(k_x,k_y)\,\hat{K}(k_x,k_y)
\end{align}
$$

Where $K$ is the Kernel belonging to the current organism, and $\hat{K}$ is the Fourier-transformed kernel. $\hat{K}$ is precomputed on startup by transforming the spatial kernel into frequency space, so the convolution step is just a pointwise multiplication of complex numbers.

## 3. Inverse FFT (back to spatial domain)

The filtered field is reconstructed via inverse transform.

$$
G(x,y) =
\mathcal{F}^{-1}\left\lbrace\hat{G}(k_x,k_y)\right\rbrace
$$

This corresponds to the spatial convolution

$$
G = F_t * K
$$

where $*$ is the convolution operator. You can find a deprecated spatial convolution implementation in [shaders/lenia.comp](../shaders/lenia.comp) for reference, but the FFT-based approach is much faster for large kernels. It also has the advantage of scaling at $O(n \log n)$ instead of $O(n^2)$, which means we can increase the size of the kernel without a noticeable performance drop.

## 4. FFT shift and normalization

The result is normalized and shifted so the kernel center aligns with the grid center. If this weren't done, the image quadrants after the inverse FFT
would be arranged incorrectly. This can be seen in the Python example below, in the top-right corner.

$$
\tilde{G}(x,y) =
\mathrm{fftshift}\left(
\frac{1}{N}\,G(x,y)
\right)
$$

where $N$ is the number of grid cells.

## 5. Lenia growth function

This implementation applies a compact polynomial growth rule.

$$
\begin{align}
\alpha(x,y) &= \max\left(0,\;1-\frac{\bigl(\tilde{G}(x,y)-\mu\bigr)^2}{9\sigma^2}\right) \\[6pt]
H(x,y) &= 2\,\alpha(x,y)^4 - 1 \\[6pt]
\end{align}


$$

Where $\mu$ and $\sigma$ are animal-specific constants.

## 6. Time integration

The field evolves using explicit Euler integration.

$$
F_{t+\Delta t}(x,y) =
\mathrm{clip}(F_t(x,y) + \Delta t \, H(x,y), 0, 1)
$$

Note: $\Delta t$ is usually a time constant. But in this simulation, $\Delta t$ is used to ensure the simulation updates in reasonable time intervals. Because we can reach framerates of over 1000FPS, $\Delta t$ needs to be adjusted so the simulation doesn't update too fast.

## 7. Layer statistics

Let the simulation domain be

$$
\Omega \subset \mathbb{R}^2
$$

After the field update, the code computes one set of summary statistics over the entire simulation domain. These include mass, center of mass, and bounding box information that are used for both display and control purposes.

### Mass

$$
M =
\sum_{(x,y)\in \Omega}
F_{t+\Delta t}(x,y)
$$

### Linear first moment

Before applying toroidal correction, the code accumulates the ordinary mass-weighted position sum

$$
\mathbf{m}_{\mathrm{lin}} =
\sum_{(x,y)\in \Omega}
\begin{pmatrix}x \\ y\end{pmatrix}
F_{t+\Delta t}(x,y)
$$

and the corresponding linear center of mass is

$$
\mathbf{c}_{\mathrm{lin}} =
\frac{\mathbf{m}_{\mathrm{lin}}}{M}
$$

### Toroidal moments

Because the world wraps, the final center of mass is reconstructed from circular moments on each axis.

For a world of width $W$ and height $H$,

$$
\alpha_x = \tau \frac{x}{W},
\qquad
\alpha_y = \tau \frac{y}{H}
$$

with $\tau = 2\pi$.

The code accumulates

$$
C_x =
\sum_{(x,y)\in \Omega}
\cos(\alpha_x)\,F_{t+\Delta t}(x,y),
\qquad
S_x =
\sum_{(x,y)\in \Omega}
\sin(\alpha_x)\,F_{t+\Delta t}(x,y)
$$

$$
C_y =
\sum_{(x,y)\in \Omega}
\cos(\alpha_y)\,F_{t+\Delta t}(x,y),
\qquad
S_y =
\sum_{(x,y)\in \Omega}
\sin(\alpha_y)\,F_{t+\Delta t}(x,y)
$$

The wrapped center-of-mass coordinates are then recovered from

$$
\phi_x = \mathrm{atan2}(S_x, C_x),
\qquad
\phi_y = \mathrm{atan2}(S_y, C_y)
$$

mapping each angle back into the coordinate domain:

$$
c_x = \mathrm{wrap}\left(\frac{\phi_x}{\tau} W,\; W\right),
\qquad
c_y = \mathrm{wrap}\left(\frac{\phi_y}{\tau} H,\; H\right)
$$

If the toroidal moment is numerically too small, the implementation falls back to the linear center of mass.

### Bounding box

$$
B =
\mathrm{bbox}\left(
\lbrace(x,y)\in \Omega \mid F_{t+\Delta t}(x,y) > 0\rbrace
\right)
$$

## 8. Final layer summary

The resulting layer summary is therefore

$$
\text{LayerInfo} =
\left(
B,\;
M,\;
\mathbf{c},\;
\mathbf{C},\;
\mathbf{S}
\right)
$$

where

$$
\mathbf{c} =
\begin{pmatrix}
c_x \\
c_y
\end{pmatrix},
\qquad
\mathbf{C} =
\begin{pmatrix}
C_x \\
C_y
\end{pmatrix},
\qquad
\mathbf{S} =
\begin{pmatrix}
S_x \\
S_y
\end{pmatrix}
$$

## Overall update rule

The full update implemented by the CUDA path is

$$
\hat{F}_t = \mathcal{F}(F_t)
$$

$$
\hat{G} = \hat{F}_t \,\hat{K}
$$

$$
G_{\mathrm{shift}} =
\mathrm{fftshift}\left(
\frac{1}{N}\,
\mathcal{F}^{-1}(\hat{G})
\right)
$$

$$
H(x,y) =
2\,
\max\left(
0,\;
1-\frac{\bigl(G_{\mathrm{shift}}(x,y)-\mu\bigr)^2}{9\sigma^2}
\right)^4
-1
$$

$$
F_{t+\Delta t}(x,y) =
\mathrm{clip}\left(
F_t(x,y) + \Delta t\,H(x,y),
0,
1
\right)
$$

[Wiki index](index.md)
