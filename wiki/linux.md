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

Run the compilation test from the project root after configuring:

```bash
ctest --test-dir build -R '^compilation$' --output-on-failure --no-tests=error
```

The test performs a clean parallel rebuild of both `lenia` and `leniadb`.

## Run

From the project root:

```bash
cd build && ./lenia
```

Run inside `build/`: fonts, animal data, and shaders are loaded through `../resources/` and `../shaders/`. Launching `./build/lenia` from the root causes the missing-font assertion.

The Python streaming server uses uv and FastAPI. Build `lenia` first, then run from the project root:

```bash
uv sync --locked
uv run uvicorn server.app:app --host 0.0.0.0 --port 9876 --workers 1 --ws-max-size 65536
```

Open `http://<server-LAN-IP>:9876/` on the viewing device. Both devices must be able to reach port 9876. The viewer provides organism selection, editable JSON configuration, start/restart, pause/resume, and stop. This initial server is intended for a trusted local network and has no authentication.

A small [pygame client](../client.py) can display the stream in a desktop window on another device. Run from the project root on that device:

```bash
uv run --extra client client.py --url ws://<server-LAN-IP>:9876/ws
```

The pygame client defaults to 100 organism slots, repeating a shuffled list of the 18 types in the `0–17` interval with balanced counts and without a square blacklist or rule filter, with simulation scale 1 and displayed bodies capped at 64 pixels on their longest axis, in a circular arena inside a 1024×1024 window at up to 60 FPS with global `pixel_size: 1` for full detail, with velocities inversely proportional to their configured pixel size. Organisms reflect at the circle edge instead of wrapping; each uses its own species kernel and smaller isolated CUDA field within one native worker. Overlapping bodies accumulate lasting injury, slowly losing mass until death. Once injured, an organism stops growing and its existing density is uniformly reduced as health falls, so damage cannot expand it or create new occupied cells. Fading preserves the last healthy heading and rendering size, with movement continuing along the existing trajectory; boundary reflections still apply. Isolated bodies keep their normal evolution. The initial preset organisms start distributed uniformly by area within 400 pixels of the arena center, inside the visible circle, with fully random headings. Newborns still enter from random boundary points on random inward trajectories. The population contains 100 slots; overlaps, fading, and hidden entries or turns can make fewer than 100 bodies visually distinguishable. Dead organisms are automatically replaced by new organisms entering from random points beyond the visible circle; death and birth events appear in the server log. Newborn types are sampled uniformly from fitting types in the configured intervals, allowing repeats of living types, always using scale 1. The smallest organism moves at 25 display pixels per simulated second; every other speed is `25 × smallest pixel size / its pixel size`, using the longest seed dimension times scale. The server recalculates all speeds after a birth, preserving movement directions. Circle-mode velocities are automatic, with a maximum of 25 and no fixed minimum of 20. The inclusive `ORGANISM_RANGES` tuple in `server/catalog.py` controls the available intervals, defaulting to `((0, 17),)`. Add more `(first, last)` pairs to extend selection. Every type within the configured intervals is eligible. Births allow types already present in living slots and use scale 1 when the seed and kernel fit the arena, keeping replacement possible when every allowed type is already alive. The legacy JSON `excluded_types` field is accepted but ignored; there is no square blacklist or kernel/growth-rule filter. Types outside the configured intervals are rejected and not selected for births. The 100-organism scene increases simulation workload, so actual FPS depends on the selected types and hardware. Supply `--config path/to/config.json` to send your own start configuration using the schema in the [technical overview](architecture.md). Close the window or press Escape to disconnect. Hold Space in the pygame client or browser viewer during a circular simulation to steer organisms toward the invisible edge at twice their normal speed; release it to restore normal speed and steer toward the center. The server receives `outward` and `inward` commands. Each organism selects its own random heading offset within about ±37 degrees every two animation seconds and gradually turns at up to 0.8 radians per animation second while preserving the current speed multiplier. Newborns also receive the current multiplier. Center-directed steering is the initial mode; hidden-boundary reflections still apply. Losing viewer focus releases the control. The project targets Python 3.14, selected by `.python-version`. The optional client dependency is pygame-ce 2.5.8, which provides the `pygame` import and a Python 3.14 wheel. The viewing device needs a graphical display but does not need CUDA. Close any browser viewer before connecting because the server accepts one viewer at a time.

The server launches `build/lenia --stream-arena` for circular arenas or `build/lenia --stream` for wrapping fields, with `build/` as its working directory. It still requires the NVIDIA GPU and a graphical desktop session: the worker uses a hidden GLFW window with CUDA/OpenGL interoperability, rather than a display-independent rendering backend. Use one Uvicorn worker; the server permits one simulation/viewer at a time.

To find square-shaped evolved states, run `uv run python square_visualizations.py` from the root in a CUDA/OpenGL desktop session. The script scans types 0–20 independently (including excluded types), evolves each for up to 600 frames at its single automatic pygame preset scale with the pygame preset’s field sizing using the actual circular arena renderer in a 1024×1024 output, and checks every 30 frames. Joblib runs two native GPU workers concurrently by default; `--jobs N` changes concurrency. A gray ASCII tqdm bar advances when each type finishes. It saves matching PNGs, a labeled `squares.png` contact sheet when matches exist, and `report.json` in `square-visualizations/` inside the project root. Use `--last 527` to inspect the whole catalog or `--scale N` to override the preset scale, or adjust `--frames`, `--scale`, `--size`, and `--output`. Detection requires a bounding box square within 2% and four edge bands (3% of body width) with at least 60% of the overall body’s active-pixel density. Any nonblack arena pixel is counted, including faint purple states. Relative density detects sparse repeated square textures as well as solid squares; `--edge-coverage` changes this ratio. The visible arena outline is masked before detection. The original separate-renderer, absolute single-edge-density scan could miss the squares displayed by the arena; its zero-match report is not conclusive. This is a visual heuristic that includes tiled fields; it does not prove a species always becomes square, and isolated evolution can differ from interacting arena states. Unsupported rules and oversized kernels are skipped. Run the WebSocket integration checks from the root with `uv run python -m unittest discover -s tests -v`. They check death detection, replacement cancellation and configuration, JPEG delivery, the 60 FPS setting, invalid configuration, the single-viewer limit, startup failure, replacement protocol serialization, and disconnect cleanup with a synthetic subprocess, without requiring CUDA. For native boundary reflection, hidden birth motion, and scattered-ring detection, build with `cmake --build build --target stream_motion_test` and run `ctest --test-dir build -R '^stream_motion$' --output-on-failure`; this test does not require a GPU. Run the optional GPU interaction regression with `LENIA_GPU_TESTS=1 uv run python -m unittest discover -s tests -p test_native_interactions.py -v` in a CUDA/OpenGL desktop session; it compares overlapping and separated pairs, checks gradual death and monotonically decreasing injured mass, and checks health reset on reseeding. Rebuild `lenia` and restart the server and client after changing native simulation code or the streaming protocol.

## Build details

The `lenia` target builds the visualization without the separate debug executable and assembly-generation targets. The `compilation` CTest test rebuilds `lenia` and `leniadb` with `--clean-first --parallel` and runs separately from other tests. The [GitHub Actions workflow](../.github/workflows/compilation.yml) runs this test on every push using Ubuntu 24.04, GCC 11, and the distribution's CUDA Toolkit. Compilation does not require a GPU or graphical display.

CMake tests the default `g++`, then installed versioned `g++-*` executables on `PATH`, by compiling a small CUDA C++20 source. It selects the first compiler that passes for both C++ and CUDA host code. No compiler version or installation path is hardcoded. The `Animal` constructor and destructor are defined in `animal.cu` so NVCC compiles the lifecycle operations for its Thrust device vector.

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
