# Wizward

Wizward is the production C++ game built on the Pixel Twins system layer.

The game logic is shared between the macOS and microcontroller targets.
Pixel Twins is included as a Git submodule under `external/pixel-twins`.

## Clone

```sh
git clone --recurse-submodules https://github.com/shuichitakano/wizward.git
```

For an existing checkout:

```sh
git submodule update --init --recursive
```
