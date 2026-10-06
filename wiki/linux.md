---
type: Documentation
title: Linux Build and Run
---

# Linux Build and Run

## Compile on Linux

Requirements: an NVIDIA GPU and driver with CUDA/OpenGL interoperability, a CUDA Toolkit with cuFFT, CMake 3.24 or newer, and a C++20 compiler compatible with your CUDA Toolkit. GLFW, GLM, ImGui, and stb are included as Git submodules.

On Debian/Ubuntu, install the build tools and X11/Wayland development packages:

```bash
sudo apt install build-essential cmake pkg-config xorg-dev libwayland-dev libxkbcommon-dev wayland-protocols
```

Install the NVIDIA driver and CUDA Toolkit separately for your Linux distribution, and ensure `nvcc` is on `PATH`.

From the project root:

```bash
git submodule update --init --recursive
cmake --fresh -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target lenia --parallel
```

## Run

From the project root:

```bash
cd build && ./lenia
```

Run inside `build/`: fonts, animal data, and shaders are loaded through `../resources/` and `../shaders/`. Launching `./build/lenia` from the root causes the missing-font assertion.

## Build details

The `lenia` target builds the visualization without the separate debug executable and assembly-generation targets. The `compilation` CTest test rebuilds `lenia` and `leniadb` with `--clean-first --parallel` and runs separately from other tests. The [GitHub Actions workflow](../.github/workflows/compilation.yml) runs this test on every push using Ubuntu 24.04, GCC 11, and the distribution's CUDA Toolkit. Compilation does not require a GPU or graphical display.

CMake tests the default `g++`, then installed versioned `g++-*` executables on `PATH`, by compiling a small CUDA C++20 source. It selects the first compiler that passes for both C++ and CUDA host code. No compiler version or installation path is hardcoded.

If no installed compiler passes, configuration stops with an explanation and the path to the probe log. Install a compiler supported by your Toolkit or upgrade the Toolkit. CUDA does not support every GCC version; see NVIDIA’s [host compiler requirements](https://docs.nvidia.com/cuda/cuda-installation-guide-linux/index.html#host-compiler-support-policy).

Explicit CMake compiler settings and the `CXX`/`CUDAHOSTCXX` environment variables bypass automatic selection. When overriding, select the same compatible compiler for C++ and CUDA host code.

CMake caches compiler choices and absolute project paths. `--fresh` regenerates the cache when changing compilers or after moving or renaming the project directory. The project always places the executable in the root `build/` directory, even if you configure elsewhere.

[CMakeLists.txt](../CMakeLists.txt) currently sets `CMAKE_CUDA_ARCHITECTURES` to `61`. The selected Toolkit must support that target, and the GPU must be compatible with the generated code. A different architecture requires changing that project setting; a command-line cache value does not override the unconditional setting.

## Startup troubleshooting

- **Undefined reference to `__cxa_call_terminate`:** the C++ compiler and linked C++ runtime may come from different GCC versions. Clear compiler overrides, reconfigure with the command above, and rebuild with `cmake --build build --target lenia --clean-first --parallel` to replace old object files.
- **Could not load font file:** run from `build/` and confirm `../resources/consolas.ttf` exists.
- **Missing GLFW dependencies:** install the X11 and Wayland development packages above. For an X11-only build, configure with `-DGLFW_BUILD_WAYLAND=OFF`.
- **CUDA compiler not found:** confirm `nvcc` is on `PATH`, or configure with `-DCMAKE_CUDA_COMPILER=/path/to/nvcc`.
- **Graphical display or CUDA/OpenGL initialization failure:** use a Linux desktop session with the NVIDIA driver and a GPU supporting the required interoperability.


[Wiki index](index.md)
