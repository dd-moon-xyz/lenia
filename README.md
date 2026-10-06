# Lenia

<p align="center">
  <img src="resources/lenia_showcase.gif" alt="Lenia showcase" width="40%" />
</p>

An interactive Lenia simulation built with C++20, CUDA, and OpenGL. Load cataloged creatures, draw and stamp organisms, and steer moving animals in a continuous cellular automaton.

This project is a fork of [DeepQuantum/Lenia](https://github.com/DeepQuantum/Lenia), focused on Linux builds and usage. See [Linux build and run](wiki/linux.md) for setup instructions.

This implementation focuses on treating Lenia creatures less like passive simulations and more like controllable agents inside a responsive GPU application.

- Loads a large library of Lenia animals from [resources/animals_dim.csv](resources/animals_dim.csv) (These are collected from the original project)
- Builds each creature from its encoded pattern, kernel parameters, and taxonomy metadata
- Runs the simulation on a 1024x1024 field in real time
- Lets you cycle through animals, resize them, reset the world, and inspect live stats
- Supports direct editing with circular drawing and stencil placement modes
- Includes a control mode that uses the creature's direction of motion and center of mass so you can nudge it left or right while it is swimming across the toroidal world
- Applies a post-processing shader over the animals that can be controlled for various effects

See the [project wiki](wiki/index.md) for documentation, Linux build instructions, and usage.

## Credits

Forked from [DeepQuantum/Lenia](https://github.com/DeepQuantum/Lenia).

Based on Lenia by Bert Wang-Chak Chan.

- [Official Lenia page](https://chakazul.github.io/lenia.html)
- [Original paper: Lenia: Biology of Artificial Life](https://arxiv.org/abs/1812.05433)
- [Original implementation](https://github.com/Chakazul/Lenia)
