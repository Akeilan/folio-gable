# Folio Gable

The 3D modeler of the Folio office suite (with Folio Page, Folio Sheet, Folio Show, Folio Post and Folio Draw — together Folio Studio), written from scratch in C++17, Qt 6 and OpenGL 3.3 for Zorin OS / Ubuntu.
It models the way SketchUp does — sticky geometry, automatic faces, inference, push/pull, the same default keyboard shortcuts — inside the suite's window: a purple title bar with the Quick Access Toolbar and command search (Alt+Q), a ribbon with KeyTips (tap Alt), and the File view for templates, recent models, export and options.

## Install

```
sudo apt install ./folio-gable_2.6.0_amd64.deb
```

Runs on Zorin OS 17 (Ubuntu 22.04) and Zorin OS 18 (Ubuntu 24.04). Launch from the app menu or `folio-gable model.gbl`.

## What's new in 2.6

- **Drawing sheets** (File › Export › Drawing sheets, the Send to LayOut command): put scenes on paper (A4 to A0, Letter, Tabloid, Arch C/D) as views, at a scale such as 1:50 for scenes in parallel projection; add dimensions that measure the model at the view's scale, labels with leaders and a title block (project, drawing, drawn by, date, scale, sheet). Several sheets per model, kept in the .gbl file; print them or save one PDF.
- **Import 3D Studio (.3ds), Google Earth (.kmz) and IFC (.ifc)**: 3DS meshes come in as groups with their materials; a KMZ's models at their places (metres east and north of the first); an IFC's building parts as groups named after them on a tag per IFC class (IfcWall, IfcSlab, ...), with their colours and units. Openings cut into walls are not cut out yet, and spaces start hidden.
- **Send in email** (File › Export): a new message in Folio Post (or the desktop's mail app) with the model, or a picture of the view, attached.
- Dark or coloured desktop themes no longer show through in light mode: Gable keeps its own palette and widget style.

## What's new in 2.3

- **Copy View as Picture** (Home › Clipboard, Ctrl+Shift+C, 2.3.1): the view goes to the clipboard as a picture, to paste into Folio Draw, Folio Sheet or other programs.

- **Materials panel › Edit**: change a material's name, colour, opacity, texture picture (load, remove, save) and tile size right in the panel, with the picture's proportions kept unless unlocked; Reset takes the texture's average colour. Each change is one undo step (a slider drag included).
- **Material collections**: ⋯ › Export materials in model / Save current material write a `.gmat` collection; Import collection adds it to the list beside Colors and Textures, and picking from it adds the material to the model.
- **Edge style settings** (Styles panel › Effects): profile width, extension length, endpoints, jitter, depth cue and halo, each on or off with its size in pixels, saved with the model and its scenes. The Sketchy Profiles style uses them.
- **Two-Point Perspective** (Camera tab): verticals stay vertical; orbiting returns to normal perspective.
- **Match Photo** (Camera tab): pick a photo, drag the two red and two green lines onto edges in it that run along the red and green axes, drag the yellow grip to where the axes meet, type the eye height, and Enter. The camera now matches the photo, which shows behind the model at that scene's camera. Edit Matched Photo moves the grips again; Turn Axes swings red and green round when they point the wrong way.
- **glTF/GLB and 3MF export**: glTF 2.0 for Blender, game engines and the web (materials, textures, transparency, optional edges); 3MF for printing, one object per solid, with colours.
- **Solid Inspector** (Solids tab, or Entity Info › Inspect on a group that isn't solid): marks holes, stray edges, faces inside, reversed faces, edges shared by several solids and groups inside, and Enter fixes what it can (fills holes, erases strays and inner faces, turns faces outward).

## What's new in 2.2

Large models (100,000+ faces, such as an imported house or a terrain) stay responsive:
- Hovering, inference and clicking use a spatial index built in the background, so they no longer scan the whole model on every mouse move.
- Move, Rotate and Scale previews redraw only what moves.
- Edits rebuild the display faster (convex faces skip the general tessellator; smooth-edge normals are worked out once per vertex).
- Undo and redo copy the model once instead of twice; Select All on 300,000 entities is instant.
- Edges and profiles draw in one pass.

`folio-gable --bench` builds three 100,000-face models and prints the timings.

Texture positioning: right-click a textured face › Texture › Position shows pins on the face — drag the red pin to move the texture, green to rotate and scale it, blue to scale and shear it (dragging elsewhere on the texture also moves it); Enter or a click away finishes, Esc cancels. The same menu resets the position, rotates by 90°/180°/270°, flips, and turns on Projected: a projected texture sampled with the Paint Bucket (Alt) paints onto other faces unchanged, so it runs across terrain or sloped faces as one picture. Textures now move with faces that are moved, rotated or scaled as a whole.

## What's new in 2.0

- Folio suite window: purple title bar with Save/Undo/Redo and command search, ribbon tabs (Home, Draw, Modify, Construct, Solids, Terrain, Camera, View, Help, and Group Editing while a group is open), KeyTips, File view with templates, recent models with thumbnails, Info, Export and Options; new suite-style icon set.
- Solid tools now handle solids that share faces (boxes on the same ground, touching walls).
- Follow Me picks small profiles reliably and runs closed paths the right way; Push/Pull no longer snaps to the face it is pushing; Intersection inference; Ctrl draws rectangles from the centre.
- Pictures can be imported as images or textures (or dropped on the window); the view exports to PDF; sections can make a group from the slice.
- Entity Info edits edge length and group position and size; Reset Scale and Zoom Selection in the context menu.

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

Middle-drag orbits about the point under the cursor (over empty space: about the selection, else whatever is in the middle of the view), Ctrl+middle-drag orbits without gravity, Shift+middle-drag pans, the wheel zooms at the cursor, middle double-click recenters.
Hold Shift to lock an inference; → ← ↑ lock red/green/blue, ↓ locks parallel/perpendicular to the last edge.
Type any value while a tool is active — it goes to the Measurements box: `2500`, `4000,3000`, `5m`, `6'4"`, `[x,y,z]`, `<dx,dy,dz>`, `24s` (segments), `x5` / `/5` (arrays after a copy).
All shortcuts can be reassigned in File › Options. Ctrl+F1 collapses the ribbon; Rectangle + Ctrl draws from the centre.

## Features

Drawing: line, freehand, rectangle, rotated rectangle, circle, polygon, arc (center, 2-point, 3-point), pie, 3D text.
Modifying: push/pull (stretch, extrude, pocket, punch-through, Ctrl new face, double-click repeat), move/copy/array, rotate/rotate-array, scale with grips, offset, follow me (path, drag-along-edges, Alt = perimeter), eraser (Shift hide, Ctrl soften), paint (Ctrl fill, Shift replace, Alt sample), intersect faces, reverse/orient faces, divide, weld/explode curve, flip along.
Solids: outer shell, union, subtract, intersect, trim, split.
Organizing: groups, components (shared definitions, make unique, glue, cut opening, always face camera), tags with visibility and colors, outliner, scenes with animated transitions and playback.
Construction: tape measure (guides, resize model), protractor, dimensions (linear, radial), leader and screen text, axes tool, section planes with cut fill.
Terrain: from contours, from scratch, smoove, drape, add detail, flip edge.
Display: wireframe, hidden line, shaded, textured, monochrome, X-ray, back edges, profiles, extensions, color by axis/tag, sky and ground, fog, sun shadows by date/time/location.
Files: native `.gbl`; import OBJ, STL, COLLADA (.dae), DXF and pictures (placed as images or used as textures; dropping a picture on the window places it); export OBJ, STL (watertight check, outward-oriented shells), 3MF, glTF/GLB, DAE, DXF, PNG/JPG at any resolution; print. Auto-save with crash recovery.

`.skp` files cannot be opened (the format is proprietary); export them from SketchUp as COLLADA (.dae) and import that.

## Build from source

```
sudo apt install qt6-base-dev libqt6opengl6-dev libqt6svg6-dev libgl1-mesa-dev libglu1-mesa-dev cmake ninja-build g++ dpkg-dev fakeroot
./packaging/build-deb.sh          # builds, runs the geometry tests, writes gable_<ver>_amd64.deb
```

`folio-gable --audit` drives the whole program with synthesized mouse and keyboard input (every tool, command, context-menu entry, ribbon button and File page) and validates the model after each step. `tools/make_icons.py` regenerates the ribbon icons.
