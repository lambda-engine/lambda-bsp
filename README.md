# LambdaBSP

The Lambda Engine map compiler. Reads a Hammer `.vmf` and writes a Source `.bsp` the engine loads.

```
GameDir.txt                     where the game repository is - everything below resolves from it
Build.bat                       builds LambdaBSP.exe into <game>\LambdaEngine\Binaries\Win64
Compile.bat startup             mapsrc\startup.vmf -> maps\startup.bsp, in the game repository
LambdaBSP map.vmf -game <mod>   the tool itself
```

No dependencies. It does not link against Unreal and is not meant to — it is a command-line program that reads
one file format and writes another, and it builds in a few seconds with nothing but MSVC.

## Why not just use vbsp

Two reasons, and the second is the one that started this.

Valve's `vbsp` is not ours to ship, and it compiles for an engine we are not running. Most of what it spends its
time on — the BSP tree, visibility, lightmaps, areaportals — exists to answer questions Unreal already answers
for us. The engine reads a BSP for its brushes, its textures and its entities, and walks none of the tree.

The second reason is a bug that cost an afternoon. `vbsp` records each material's pixel size into the BSP, and
when it cannot find the material it writes **zero** — silently, with a warning buried in a log nobody reads.
Texture coordinates are stored in texels per world unit, so a renderer divides them by that size, and a zero
divides into something several hundred times too dense. The map still compiles. It still opens in Hammer looking
perfect, because Hammer reads the VTF itself and never consults the compiler's record. It is wrong only once it
is in the game, and it looks like a renderer bug rather than a compile one.

LambdaBSP mounts the same content the game does — it reads the mod's own `gameinfo.txt`, expands
`|gameinfo_path|` and `|steamlibrary_path|` exactly as the engine does, and opens the same VPKs — so it sees
what the game will see. When a material still cannot be found it says so by name and by count, at the end,
where it cannot be missed.

## What it does

* **Brush geometry.** Each side's polygon is derived by clipping a plane-sized square by the brush's other
  planes (Valve's `BaseWindingForPlane` / `ChopWindingInPlace`, ported from `utils/common/polylib.cpp`).
* **CSG.** A face with another brush standing on it is cut down to the parts still showing. Two brushes sharing
  a surface would otherwise flicker against each other, and two stacked back to back would each draw a face
  nobody can see. Both cases are decided on plane identity rather than on comparing floats, which is what the
  paired plane pool is for.
* **Texture data.** Real dimensions and real reflectivity, read from the VTF; texture axes and shifts converted
  the way `TexinfoForBrushTexture` does, including the detail that the axes carry the scale and the shift does
  not.
* **Surface flags and contents** from the `%compile*` vars in the VMT, in the order vbsp tests them.
* **Models.** The world is model 0; each brush entity gets its own, with its geometry written relative to its
  own origin — from an origin brush if it has one — so the engine can rotate a door about its hinge.
  `func_detail` is folded into the world and leaves no entity behind.
* **Entities**, with `id` carried across as `hammerid` so an entity misbehaving in game can be found in
  Hammer, and the `connections` block flattened into keyvalues the way vbsp does it
  (`CMapFile::LoadConnectionsKeyCallback`) - that block is where every wire the mapper drew lives, and
  an entity that keeps all its keyvalues but loses its outputs looks completely intact until nothing
  responds to anything.

## What it does not do, and why

These are absences by choice, not gaps waiting to be filled — except the last two.

* **No BSP tree, no visibility, no areas.** Unreal culls. `headnode` is written as `-1` rather than pointing at
  a node that is not there.
* **No lightmaps, no `vrad`.** The engine lights the map with Unreal's own lights, built from the `light`
  entities.
* **No void-face removal.** vbsp floods the map from its entities and throws away everything the flood cannot
  reach, which is also how it detects leaks. LambdaBSP keeps those faces: a sealed room compiles to its inner
  surfaces *and* the outer shell nobody can see. It costs geometry — roughly twice as many faces on a simple
  map — and nothing else, since the outer faces are back-facing from anywhere a player can stand. **This is the
  next thing worth building**, and it brings leak detection with it.
* **No displacements.** A `dispinfo` side is skipped rather than emitted flat, because a flat face where a
  displacement should be is a lie the renderer cannot tell from real geometry. The compile reports how many.
* **No static props, no cubemaps, no embedded pakfile, no water.**

## Verification

Compiled against `startup.vmf` and compared with the same map compiled by Valve's `vbsp`:

| | vbsp | LambdaBSP |
|---|---|---|
| `DEV/DEV_MEASUREGENERIC01B` | **0 x 0** | 128 x 128 |
| `DEV/DEV_MEASUREWALL01A` | **0 x 0** | 512 x 512 |
| `DEV/DEV_MEASUREGENERIC01` | 128 x 128 | 128 x 128 |
| world bounds | -320,-320,-64 .. 320,320,320 | identical |
| faces | 16 | 36 (the void-facing shell, above) |

Rendered from a fixed camera in the engine, the two are indistinguishable — same texture scale, same measure
labels, same lighting. The zeroes in the left column are the bug this program exists to prevent.

A second map exercises what `startup.vmf` does not: a brush entity (geometry comes out origin-relative, keyed
`"model" "*1"`), a `func_detail` (folded into the world, no entity emitted), and two overlapping brushes (24
brush faces become 36 surfaces, 2 buried faces removed).

## Layout

```
src/Math.h          vectors, planes, bounds
src/Winding.*       polygon from planes, clipping, splitting
src/KeyValues.*     the VMF / VMT / libraryfolders.vdf grammar
src/Vpk.*           Source 1 VPK reading
src/FileSystem.*    gameinfo.txt search paths, Steam libraries
src/Materials.*     VMT -> dimensions, reflectivity, %compile flags
src/BspFile.*       on-disk structs and the lump writer
src/Compiler.*      the pipeline
src/main.cpp        the command line
```
