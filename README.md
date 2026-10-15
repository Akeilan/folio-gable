# Folio Gable

A native 3D modeler for Zorin OS / Ubuntu, written from scratch in C++17, Qt 6 and OpenGL 3.3.
It works the way SketchUp works: sticky geometry, automatic faces, inference, push/pull — with the same default keyboard shortcuts.

## Install

```
sudo apt install ./folio-gable_1.2.0_amd64.deb
```

Runs on Zorin OS 17 (Ubuntu 22.04) and Zorin OS 18 (Ubuntu 24.04). Launch from the app menu or `folio-gable model.gbl`.

## Keyboard (SketchUp defaults)

| Key | Tool | Key | Tool |
|---|---|---|---|
| Space | Select | M | Move (Ctrl = copy) |
| L | Line | Q | Rotate |
| R | Rectangle | S | Scale |
| C | Circle | F | Offset |
| A | 2-Point Arc | E | Eraser |
| P | Push/Pull | B | Paint Bucket |
| T | Tape Measure | G | Make Component |
| O / H / Z | Orbit / Pan / Zoom | Shift+Z | Zoom Extents |
| K | Back Edges | Ctrl+G | Make Group |

Middle-drag orbits, Shift+middle-drag pans, the wheel zooms at the cursor, middle double-click recenters.
Hold Shift to lock an inference; → ← ↑ lock red/green/blue, ↓ locks parallel/perpendicular to the last edge.
Type any value while a tool is active — it goes to the Measurements box: `2500`, `4000,3000`, `5m`, `6'4"`, `[x,y,z]`, `<dx,dy,dz>`, `24s` (segments), `x5` / `/5` (arrays after a copy).
All shortcuts can be reassigned in Window › Preferences.

## Features

Drawing: line, freehand, rectangle, rotated rectangle, circle, polygon, arc (center, 2-point, 3-point), pie, 3D text.
Modifying: push/pull (stretch, extrude, pocket, punch-through, Ctrl new face, double-click repeat), move/copy/array, rotate/rotate-array, scale with grips, offset, follow me (path, drag-along-edges, Alt = perimeter), eraser (Shift hide, Ctrl soften), paint (Ctrl fill, Shift replace, Alt sample), intersect faces, reverse/orient faces, divide, weld/explode curve, flip along.
Solids: outer shell, union, subtract, intersect, trim, split.
Organizing: groups, components (shared definitions, make unique, glue, cut opening, always face camera), tags with visibility and colors, outliner, scenes with animated transitions and playback.
Construction: tape measure (guides, resize model), protractor, dimensions (linear, radial), leader and screen text, axes tool, section planes with cut fill.
Terrain: from contours, from scratch, smoove, drape, add detail, flip edge.
Display: wireframe, hidden line, shaded, textured, monochrome, X-ray, back edges, profiles, extensions, color by axis/tag, sky and ground, fog, sun shadows by date/time/location.
Files: native `.gbl`; import OBJ, STL, COLLADA (.dae), DXF; export OBJ, STL (watertight check, outward-oriented shells), DAE, DXF, PNG/JPG at any resolution; print. Auto-save with crash recovery.

`.skp` files cannot be opened (the format is proprietary); export them from SketchUp as COLLADA (.dae) and import that.

## Build from source

```
sudo apt install qt6-base-dev libqt6opengl6-dev libgl1-mesa-dev libglu1-mesa-dev cmake ninja-build g++ dpkg-dev fakeroot
./packaging/build-deb.sh          # builds, runs the geometry tests, writes gable_<ver>_amd64.deb
```
