# Graphical remaster: enhanced graphics

Status (2026-09-21): original and enhanced graphics are selectable on the same
simulation. Enhanced mode draws the three players (every animation frame) and
the enemies, bosses, items and effects of all eight rounds with generated 2x
art. The art comes from an offline pipeline that redraws the original frames;
**it is not hand-drawn**, and any frame can be overridden with hand-made art.
Backgrounds, the HUD and text are unchanged.

Read this before touching `src/render/art_catalog.*`, `sprite_probe.*`, `scene_light.*`, `scene_particles.*`, `scene_weather.*`,
`VdpScene::enhancedSprites`, `src/headless/extract_frames.cpp`, the loader in
`src/dreamcast/renderer_kos.cpp` or the art tools. Everything derived from the
ROM (frames, sheets, packages) stays under `build/`, out of git.

## Quick start

```sh
tools/build-headless.sh            # host build: extraction and previews
tools/build-profile-core.sh        # reference core, for the bot runs
build/tools-venv/bin/python3 -m pip install numpy pillow
tools/make-art-set.sh              # everything below; about 2 minutes -> build/art/SORART.PAK
./build-and-run.sh                 # test ELF with the package embedded, enhanced on, Flycast muted
```

Review the result in `build/art/sheets/<character or type-XX>.png` (original
pixel-doubled above, generated below), `build/art/frames/*.png` (every frame)
and `build/art/SORART.json` (frame counts, palette error, PowerVR bytes per
round). L + R in game opens the options menu; GRAPHICS switches modes and
ANIMATION turns on in-between poses (see Smooth animation), LIGHTING turns
on shadows and light (see Dynamic lighting) and WEATHER the round's rain, wet
ground, haze, mist and lightning (see Weather).

Host preview of a replay, original left and enhanced right, with a text file
per frame listing the art drawn:

```sh
SOR_ART=$PWD/build/art/SORART.PAK SOR_ENHANCED_CAPTURE=$PWD/build/cap:1500:2700:100 \
  build/headless/sor-headless "$SOR_ROM" REPLAY.bin /dev/null
```

## How an object becomes replacement art at run time

The Genesis draws everything as hardware sprite pieces. SoR builds its sprite
table each update from object records: `emit_object_sprite_mapping` (`$AF46`)
resolves an object's animation frame to a frame record in ROM, computes the
object's screen anchor (its feet) and emits one table record per visible piece.

- **Sprite probe** (`src/render/sprite_probe.*`, hooks inserted into the
  translated code by `tools/sprite_probe_patches.py`): while the game builds
  its table the port records, per object, the frame record's address
  ("mapping"), mirroring, anchor, tile base (`+$0E`), animation set (`+$04`)
  and the range of table records it emitted. Recording changes no game state
  or emulated time.
- **Matching the displayed table**: the RAM table reaches VRAM by DMA at the
  next graphics VBlank, so the probe keeps two builds and uses the one whose
  records are byte-for-byte those in VRAM; if neither matches, the frame is
  drawn from the original pieces.
- **Art key** (`art_catalog.hpp`): the mapping address plus `colour_key()` of
  the sprite's CRAM line *over the entries the object's art uses* (each frame
  carries that mask). Why not just the palette line: enemy families share
  frames and a line while the game loads different colours (green, purple and
  yellow Signals), and replacement art has its colours baked in. Why not the
  whole line: lines also hold colours the object never uses, which stages
  cycle; keyed on those, one enemy looked like 40 different ones.
  `tools/art_key.py` computes the same key.
- **Fades and flashes** (`fade_of` in `art_catalog.cpp`): the game fades a line
  by subtracting a step from each colour channel, clamped at black (each
  channel has its own step: red goes first), and flashes by adding one, clamped
  at white; 2,044 and 122 of the transient looks seen in the sweeps are exactly
  that. Each art frame carries the CRAM line it was made from, so when the key
  does not match, a line that is the frame's colours changed that way still
  finds the frame, with a tint: a scale per channel for a fade (the ratio of
  the line's brightness after and before, standing in for the subtraction) or
  an offset for a flash. The PowerVR applies it as vertex colour and offset
  colour (art headers enable the offset colour); the host preview does the same
  arithmetic. Any other change of colours (a palette swap, a look whose art is
  not loaded) finds nothing, and the object is drawn from its original pieces
  in the game's colours.
- **Enhanced scene** (`VdpScene::enhancedSprites`): an object with art becomes
  one quad in its first record's slot; every other sprite piece becomes 8x8
  cells drawn as their own quads. Depth is the piece's priority layer (3 or 6,
  as in the original renderer) plus its link order, so art keeps the original
  order among sprites and against the planes. Mirrored frames flip the art
  about the anchor. The software sprite layers are not built in enhanced mode:
  their other product, the VDP's sprite overflow/collision status bits, is
  never used by the game (the VBlank handler discards the status read, and
  replays with both bits forced clear keep every frame's RAM equal). Per-line
  sprite limits therefore do not apply (no dropout).
- **Sprite masks**: the game hides what passes behind the HUD (a player
  dropping in at the start of a round) with a VDP sprite mask: a sprite at
  x = 0 blanks every later sprite on its lines. The enhanced scene applies the
  same rule: art is clipped to its longest run of unblanked lines, and blanked
  cells are left out.
- **What is loaded** (`load_selection` in `renderer_kos.cpp`): all the art does
  not fit in PowerVR memory (about 4.1 MB free, 4.6 MB while enhanced mode
  frees the two software sprite-layer textures). Each page names its rounds
  and, for player art, its character. Every frame the runtime reports the
  round (`$FFFF02`) and the characters in play (players' animation sets) via
  `platform_game_state`; when they or the graphics mode change, pages are
  freed and loaded (0.5-1 s, at round changes and when a player joins). Pages
  are stored most important first; if memory runs out the rest are marked
  unloaded and their frames fall back to the original pieces. Main RAM is the
  other constraint (the game leaves 1-2 MB of heap): the package stays
  zlib-compressed in RAM (1.3 MB) and one page is inflated at a time.

Package format `SORART06` (`04` and `05` still load): see the header comment of `art_catalog.hpp` (three
255-colour palettes in PowerVR palette banks 1-3; bank 0 holds the tile
palettes; 8-bit pages 512 wide, 64-512 high).

## The art pipeline (`tools/make-art-set.sh`)

1. **Player runs** (reference core + `bot-play.py`, then replayed natively):
   Adam, Axel (RIGHT on the select screen) and Blaze (LEFT) through the
   scripted Round 1, action and combat scenarios and two bot runs. They supply
   the players' CRAM line and check step 3.
2. **Round sweeps** (native headless, `SOR_CHEATS=ROUND`: start at that round
   with infinite health, lives and specials): an open-loop script walks,
   punches in both lanes and calls the police every sixth cycle; Round 8 runs
   right to left, so its script is mirrored. Scripted runs also hold every
   enemy and boss at one hit point and bring awake, grounded ones into player
   1's lane within reach and on screen (`Menu::apply`, `weakenEnemies_`): an
   open-loop script cannot line up with enemies, and without this the Round 8
   sweep stalled at the first juggler. With it every sweep clears its round,
   and Round 8 runs through Mr. X to the ending. Only frames drawn during
   rounds are extracted (title, menu and cutscene objects are not replaced),
   and an object's `+$04` counts as an animation set only if the frame on
   screen is one of its records (cutscene objects keep other data there). `SOR_EXTRACT_FRAMES=dir` saves
   every object frame drawn, and, because everything but the players keeps its
   art resident in VRAM, **renders the whole animation set** of each object
   that has been on screen for 45 frames. `index.json` records for each frame
   the mask, CRAM line, rounds seen, object types, and how the set rendering
   compared with direct captures (`check`: confirmed / contradicted /
   set_only / direct / direct_culled). Contradictions are a handful per run
   (effects whose tiles are rewritten); confirmed frames are in the hundreds.
3. **`tools/extract-player-frames.py`**: player art is not resident (each frame
   record names two ROM-to-VRAM DMA records), so all 205 player frames come
   straight from the ROM: animation sets `$53EFE` Adam, `$49AE0` Axel, `$5E90A`
   Blaze; frame record = piece count, 2+2 box ids, upper and lower art ids
   (tables `$1A160` -> VRAM `$B000`, `$1A53E` -> `$B400`), 5-byte pieces. Every
   frame the replays also drew matches pixel for pixel.
4. **`tools/make-enhanced-art.py`**:
   - drops looks that are fades or flashes (every colour darker or lighter
     than a longer-seen look of the same frame) or were barely seen;
   - redraws each frame: Scale2x twice then averaged back to 2x (anti-aliased
     interior edges, rounded silhouette, 1-bit alpha for PowerVR punch-through),
     then an edge-preserving (bilateral) filter that turns the 16-colour
     originals' stepped ramps and dithering into continuous shading while
     outlines stay crisp;
   - quantises three palettes (players; objects seen in every round; the rest)
     with farthest-point seeding and k-means, no dithering;
   - crops, packs pages per (rounds, palette, character) group, orders them by
     importance and writes `SORART06`.
   `--style placeholder` (or `ART_STYLE=placeholder`) makes pixel-doubled
   frames with a cyan outline and a magenta anchor cross instead, to check
   alignment and coverage. `--override DIR` (or `ART_OVERRIDE=dir`) takes
   hand-made `<MAPPING>_c<KEY>.png` frames (RGBA, twice the extracted frame's
   size, same anchor; names as in `build/art/frames/`).

## Smooth animation (in-between poses)

The game moves objects every tick (60 Hz) but draws few poses: most player
animations have 3-4 (a walking pose is held 8-12 ticks, an attack pose 2-4), so
smoother animation needs new poses. ANIMATION: SMOOTH in the options menu
(L + R; `SOR_SMOOTH=1` starts with it on), with enhanced graphics, shows them:

- **Run time** (`VdpScene::smooth`): the scene remembers the pose (mapping) of
  each object with art. When an object's mapping changes from A to B, with the
  same animation set and facing, and the catalog has an in-between for (A, B)
  in the line's colours, that art is drawn for the first 2 builds of the sprite
  table (`INBETWEEN_TICKS`), then B. No in-between: the cut the game always
  made. Drawing only: the simulation, its timing and the original mode are
  untouched; the scene is not reused from the cache while one is on screen.
- **Package**: an in-between is a frame with `from` set (`SORART05`), on pages
  of their own (page character bit 7) stored last. They are selected only while
  smooth animation is on, and being last they take only the PowerVR memory the
  ordinary art leaves: they can never push a pose out.
- **Generated in-betweens** (`tools/make-inbetweens.py`): every pair of poses
  that follow one another in an animation record (players' sets, and every
  other set found in the ROM whose frames were all extracted: 975 pairs with
  their looks), by dense correspondence at the original resolution and colours,
  then redrawn like any frame. **It rarely works, and the tool knows:** the
  poses are too far apart (legs swap sides between walking poses, an arm is
  thrown out in one step), correspondence invents tangled limbs, and the
  gates -- similar silhouettes, no part 3 pixels thick present in one pose
  only, both poses agreeing where they differ -- reject 955 of the 975. The two
  distinct pairs kept are one enemy's idle sway, there and back. A wider search (20 pixels) did
  not help. Flow-style interpolation is a dead end for this art.
- **Hand-made in-betweens** are therefore the way to fill this in. Every pair's
  draft is in `build/frames-inbetween/<FROM>_<MAPPING>_c<KEY>.pam` (and on the
  sheets in `build/art/sheets-inbetween`, rejected ones under a red bar); a
  PNG of that name, RGBA at twice the draft's size with the anchor in the same
  place, in the `ART_OVERRIDE` directory is packed whatever the gates said.
  Checked end to end on the host with the rejected drafts standing in: Adam's
  48 in-betweens are 590 KB of PowerVR memory and are drawn on his pose changes
  (`SOR_SMOOTH=1` with `SOR_ENHANCED_CAPTURE` at a step of 1; the frame lists
  name the `from` pose).

## Dynamic lighting (shadows and light)

The game has no light: a sprite has the same colours wherever it stands, and
nothing casts a shadow. LIGHTING: DYNAMIC in the options menu (L + R; on by
default, in the Dreamcast build and the host preview: `SOR_LIGHTING=0` starts
without it), with enhanced graphics, adds both. Drawing only, from the VDP's state
and the sprite probe; the simulation and the original mode are untouched.

**The PowerVR has no programmable shaders**, so there are none: everything is
what its fixed pipeline does per quad -- texture x vertex colour + offset
colour, interpolated between the corners -- plus the translucent list (enabled
for this in `game.cpp`; it draws nothing else). `src/render/scene_light.*`
computes, in integers, the same values for the Dreamcast and the host preview
(`raster_enhanced`).

- **The backdrop as lights** (`SceneLight::build`, `collect`): planes B and A
  as they are on screen (not the HUD's window plane) are reduced to a 20 x 16
  grid: per cell a mean colour and, with each texel counted by its brightness
  squared (strongest channel, so a saturated orange window counts as much as a
  pale lamp), an RMS brightness and the colour of what is bright. Cells above
  the *wall line* that outshine the screen's mean are lights (merged two by
  two, at most 80), standing in the wall at their place and height with their
  colour and power. Neon that cycles colours changes the light with it. Every
  other tile (by pattern number: stable under scrolling) gives two texels; the
  grid is rebuilt at most every fourth tick of the game while the planes
  scroll, and every 16 ticks otherwise (ticks, not scenes built: host captures
  that skip frames would otherwise light from a grid hundreds of frames old).
- **The whole scene lights an object** (`SceneLight::shade`): every light
  counts, by its distance over the ground (a screen line is about three of the
  ground; the object's distance from the wall comes from its ground line), into
  a left and a right group (a light right behind the object is both).
- **Form, not a flat card** (`CornerLight::column`): the art's quad is a strip
  of 5 columns of vertices. Each column takes the light a cylinder's surface
  there would take from lights 60 degrees to each side (`facing`), plus a fifth
  of all the light as fill: bright towards the lights, a core of shade, the far
  edge in shade. Each vertex gets a scale (a little of the screen's hue and of
  the round's sky, more of the light's, brighter the more reaches it; the feet
  darker and in the ground's colour) and an offset (added light). The game's
  fade or flash tint is folded in. No texture memory: a normal map or a second
  shaded copy of the art would not fit (the art already overflows in rounds 5,
  6 and 8).
- **Rim light** (`RimLight`): the art again as a flat colour (vertex colour
  black, offset colour the light's, towards white), translucent, moved
  `RIM_SHIFT` art pixels towards a side's lights and drawn just behind the
  art (depth - 0.004): what shows is a thin lit edge on the side facing the
  lights, stronger the more that side outshines the other. No texture memory.
- **Rounds** (`light_profile`): exposure, shadow darkness, spill, tints, rim
  and the *sky* per round: the sky tints everything a little and casts a third
  shadow with a fixed direction that takes a share of the darkness. Round 1 is
  all shop windows; the beach (3) is the moon, up on the left, and no spill;
  the bridge (4) and the lift (7) have no wall to spill from; the factory (6)
  is harsh; the ship (5) and the headquarters (8) are warm rooms. The runtime
  passes the round (`VdpScene::round`).
- **Shadows**: away from each group's centre of power (and the sky's, above), the object's art again,
  black, bilinear, sheared from the ground line towards the viewer (the lights
  are behind the playfield): lean and length follow (object - light) / the
  lights' height, so shadows swing round as a character passes a shop window,
  lengthen towards the viewer and shorten under high lights; the two share
  `SHADOW_ALPHA` by power. Plus a contact shadow (a dark ellipse) under the
  feet that shrinks with height in a jump. Depth 2.85-2.9: over the low
  planes, under every sprite and the high-priority tiles (the foreground car
  and hydrant cover them). No extra texture memory. The original game draws
  no shadows at all.
- **Spill**: the lights' colour is added to the ground below the wall line, per
  column, strongest for lights near the ground, rising over `SPILL_RISE` lines
  (the wall line is not known to a line) and fading over `SPILL_DEPTH`:
  untextured Gouraud quads, additive.
- **Objects that are light** (`light_kind`): the police's napalm (`$0E`), the
  bazooka's flame (type `$05`, frames `$070C1F`- except the grey smoke
  `$070C47`-`$070C5B`; the car is `$070B20`-) and hit sparks (`$49`).
  The fire the round 6 bosses breathe is frames `$02F3B2`-`$02F42F` of their
  own type (`$57`/`$97`). Their colour (the bright entries of their CRAM line)
  is added to the vertices of objects near them, accumulated over every flame
  and then passed through a soft ceiling (`GLOW_MAX`, `GLOW_KNEE`: the police
  special's 18 flames used to stack to +255 and blow enemies out to white);
  fire and flame put a pool of light on the ground and a wide faint glow on the
  wall behind them. An object drawn from its pieces (art not loaded) gets the
  same light as one flat tint, scale and added light both: the renderer sets
  the offset-colour bit on its copy of the tile header.
- **Lamps on the ground** (`collect`, `Light::low`): a cell from one row above
  the wall line down that is well brighter than the screen and coloured (or
  very bright) is a light standing on the ground at its place, not part of the
  wall: the amber lamps along the bridge's parapet (round 4). They light
  objects by their own distance over the ground and put a small pool around
  themselves; pale ground (the bridge's concrete) is not coloured and does not
  qualify. (Mr. X's
  gun flash could not be told from his other frames in the extracted art: not
  a light.)
- **Particles** (`scene_particles.*`): embers rise from fire, the bazooka's
  flame throws embers and sparks and its smoke lingers, a hit spark bursts
  into sparks, feet that land (a jump, a knockdown: `+$18` back at the ground's
  value) raise dust, a prop that was on screen and is gone has broken and
  throws pieces, rain (type `$17` on screen) glints on the ground. Up to 96, their own random numbers, a tick per sprite-table
  build, x kept in the world (plane A's scroll is the camera), not drawn over
  the HUD; additive (or covering, for smoke) quads of one 32 x 32 radial
  texture. They move without invalidating the cached scene
  (`VdpScene::particlesStep` runs on reused frames too; the renderer rebuilds
  only their quads).
- **The ground line and the wall line**: a sprite's y is `+$14`/2 + `+$18`
  (`$AF60`); `+$18` is the round's ground value (160, 168, 136 on the lift...)
  and less in the air. The scene takes as the ground the `+$18` most playfield
  objects share, or one a lone object has kept for 8 builds, so a jumping
  player's shadow stays on the ground. The wall line is `WALL_ABOVE_LANES`
  above the farthest ground line anything has stood on in the round (pale
  ground behind an object must not count as a light). The probe records `+$18`
  (`level`) and `+$01` bit 1 (`screen`: placed on the screen, not the world).
- **Only in play**: the runtime says whether a round is being played (mode
  `$16`); outside it (the title, menus, cutscenes, the ending) the renderer
  turns the lighting off, so no pool of light or shadow reaches the character
  select.
- **Which objects** (`in_playfield`): players, enemies, bosses, props, weapons
  and pickups are lit and cast shadows. Scenery made of sprites (awnings,
  rain), captions and effects are left alone. An object with no art loaded is
  drawn from its pieces with one flat light (scale and added light) and no
  shadow.

Cost (Flycast, 2026-09-21, lit against unlit, enhanced graphics in both):

| replay | late frames | audio underruns | scene step |
|---|---|---|---|
| action replay, 1,611 gameplay frames | 1 against 0 | 5 against 5 | 1.3 against 0.76 ms |
| all of Round 1, 26,545 gameplay frames | 80 against 16 | 474 against 133 | 1.5 against 0.68 ms |

So it is not free: busy stretches of a whole round run over the frame more
often, and the audio underruns with them. The PC sampler
(`SOR_PC_PROFILE=1`) puts `SceneLight::build` at 1.25 % of gameplay time and
the integer divisions at 0.75 %; the rest is more quads to build and submit.
PowerVR memory is unchanged but for a 2 KB texture; main RAM holds about
130 KB more of packets. What was slow on the way, so as not to repeat it:
divisions per tile quad in `SceneLight::build` (4.2 ms, 24 frames dropped in
the action replay); particles invalidating the scene cache (every frame
rebuilt while one was alive); building the grid every build and collecting
the lights every build (147 late frames and 526 underruns over Round 1; now
every fourth build while scrolling, and lights only when the grid or the wall
line changes). Next candidates if it must be cheaper: shade objects every
other build, fewer shadow quads when many objects are on screen. Flycast does
not model cache misses: measure on hardware.

Art, not light: the round 3 boss (type `$30`, back in rounds 5 and 8) looked
scrambled while moving. His art is streamed like the players', so the sweeps'
whole-set renderings read other frames' tiles from VRAM; `index.json` shows it
(`contradicted` against the direct captures). `make-enhanced-art.py` now drops
set-only frames of any object type with a contradiction in that run and ranks
direct captures with confirmed ones: 1,002 frames in the set, he is drawn
from his original pieces except in the poses captured directly.

Limits: form is a cylinder's, the same for every pose (the PowerVR's bump
mapping would need a 16-bit normal map per page, and art already fills its
memory); the light is only what the backdrop's pixels say;
shadows are darker where two cross; a sprite mask (dropping in behind the HUD)
has no shadow; backdrops are relit only by spill and by fire's glow; steam
and other scenery effects that are part of the planes have no particles
(nothing in the sprite table says where they are).

Check it on the host: `SOR_ENHANCED_CAPTURE` (lighting is on unless `SOR_LIGHTING=0`); each frame's
text file lists the corners' light, shadows (lean/length@alpha), the ground
line, pools, the wall line, the lights' count, the spill and the grid.

## Weather (rain, wet ground, haze, mist, lightning)

The game has no weather either. WEATHER: ON in the options menu (`SOR_WEATHER=1`
starts with it on, in the Dreamcast build and the host preview), with dynamic
lighting, gives each round an atmosphere from a profile of its own. Drawing
only, like the lighting, and built from what the lighting already knows: the
wall line, the lights in the wall, and the art standing on the ground.
`src/render/scene_weather.*` holds the profiles, the two textures and the
quads; `VdpScene` ticks it with the particles; the renderer and `raster_enhanced`
draw the same quads.

- **Profiles** (`weather_profile`): per round, how much rain, how wet the
  ground is, the haze and its colour, the mist, the lights' shafts and how often
  lightning strikes. Rain falls on the street (1), the bridge (4) and the lift
  (7); the street and the bridge are wet and stormy; the inner city (2), the
  beach (3) and the bridge are hazy, the beach and the bridge misty, the
  factory (6) full of steam; the ship (5) and the headquarters (8) are rooms
  and get nothing. Which rounds get what is taste: the table is the place to
  change it.
- **Rain**: two sheets of a 64 x 64 texture of eleven long, thin streaks
  fading at both ends (generated at start-up, `weather_texture`; wraps),
  scrolled by the weather's time: one in front of everything (big streaks,
  fast, leaning with the wind), one behind the characters and in front of the
  wall (smaller, slower, fainter, moving with the world a little), both added
  to the picture. Not particles: ninety-six drops would
  look like snow, and the particles are busy with fire and dust. The particles'
  splashes on the ground come with it, as they do for the game's own rain
  (type `$17`).
- **Wet ground**: the art of each object standing on the ground drawn again
  below its feet, flipped, dimmed and fading away from them (`REFLECT_LENGTH`
  of its height, the shadow headers: no texture memory), mirrored in the ground
  line so a jumping object's reflection drops away from it, fainter with
  height; and the wall's strongest lights (up to twelve) smeared down the
  ground below them: two halves per light, full in the middle and nothing at
  the sides, widening and fading, broken into wet patches by the mist's noise
  texture, which moves with the world. Flat quads read as coloured blocks on
  the street; this was the first thing to look wrong. Both under the sprites,
  over the shadows.
- **Haze** (`weather_fog`): the backdrop's tiles take the fog's colour by their
  height, full 80 lines above the wall line and none 40 lines below it, the far
  plane (B) fully and the near one (A) five eighths: vertex colour (what is
  kept) and offset colour (the fog added) on the tile quads themselves, so no
  extra quads and nothing per pixel. The tile packets are rebuilt when the
  round or the wall line changes (`packetsFogKey`). The HUD's window plane is
  never hazed. The PowerVR's own table fog was not used: it works on z, and
  here z is the Genesis priority layer, not distance.
- **Mist**: sheets of soft tileable value noise (the second texture), in the
  fog's colour towards white, covering: two pairs at the ground line (peaking
  16 lines below the wall line, gone 70 below it) at different scales and
  drifts so that the pattern does not show, and a faint veil in front of
  everything, thicker low down. They drift with time and with the camera at
  less than its speed, so they read as mid-distance.
- **Shafts and halos**: with fog, each light in the wall above the wall line
  throws a fan of its colour (towards the fog's) down to the ground line, added,
  fading downwards (up to twelve); and glows through the fog with a pool of the
  existing radial texture at its place.
- **Lightning** (`Weather::advance`): a strike every 500-1,900 ticks scaled by
  the profile, sometimes twice; a flash held two ticks then dying away by three
  quarters a tick. While it shows: a white-blue quad added over everything, and
  the round's light profile changed for the build (`flashProfile_`): the sky
  is the light, its shadow takes over with a lean from the bolt, so every
  shadow swings under the flash. A flash is another scene (`builtFlash_`): the
  cache is not reused while it changes, a few frames per strike.
- **Only in play**: the runtime says whether a round is being played (mode
  `$16`: not the title, the menus, cutscenes or the ending) and the renderer
  turns the weather off outside it, so no rain or haze reaches the title or the
  character select. Within a round it draws once the game has displayed a
  sprite-table build; while the game is paused (no build for 30 frames) the
  rain stands still rather than vanishing.
- **Where it draws** (depths): between the planes (1.5, unused so far), over
  the ground under the sprites (2.92 reflections, 2.95 sheets, smears, shafts),
  or over everything under the particles (6.9). Nothing over the HUD's 36 lines
  but the flash. The quads are rebuilt every frame like the particles, without
  touching the scene cache.

Cost (Flycast, 2026-09-23, the action replay of 1,611 gameplay frames, weather
against none, enhanced graphics and lighting in both): 1 late frame against
0, p95 frame 20.5 ms in both; the scene step 1.41 against 1.35 ms and building
the commands 0.56 against 0.38 ms (the weather quads and reflections every
frame; the haze on the tile packets while the planes scroll, from per-line
tables). docs/OPTIMIZATION_LOG.md has the before and after. What Flycast does
not model is the PowerVR's fill for the translucent sheets: if hardware shows
it, drop the far rain sheet first, then the second mist pair. Memory: two
8 KB textures in PowerVR memory; about 25 KB more of packets in main RAM. If
it must be cheaper: fewer smears and shafts, one rain sheet, the mist's front
veil dropped. Check it on the host: `SOR_WEATHER=1` (or `2`: lightning strikes two
captured frames in, for a step of 1) with lighting on (the default) and
`SOR_ENHANCED_CAPTURE`; each frame's text file lists the profile, the flash,
the weather's time and every quad. Note that a capture step over 30 frames
resets the weather's clock (as it does the particles), so stills show the
rain at time 0.

## Current set (2026-09-21)

1,002 frames in 70 looks (205 player frames; 350 transient looks dropped), 63
pages, 1,406,417 package bytes. Mean palette error 3.7 of 255 per channel.
PowerVR bytes with one player: 2.6-4.0 MB depending on the round (one
character is 0.75 MB); two different characters in rounds 5 and 6 exceed the
budget by a few hundred KB, and the least-seen pages fall back.

Flycast, two-player bot replay (8,402 frames), enhanced: 7,143 VBlanks / 7,133
flips, as before the enemy art; audio checksums unchanged. The disc build
loaded Round 1 plus two characters into 4,456,448 bytes with 165,704 free.

Coverage: every round is swept to its end, Round 8 through Mr. X (types
`$33`-`$38`). Weapons in a player's hands are separate objects and are covered.
Left as they are: the character-select portraits, the HUD (excluded by
`WORLD_TYPES`), title, menu and cutscene objects, and two pictures wider than
a 512-texel page. Round 8 holds every returning boss and wants 6.6 MB: pages
are ordered by importance (bosses weighted up, since the sweeps fell them in
one hit) and what does not fit falls back to the original pieces.

## Known limits

- Generated art cannot add detail the 16-colour originals never had; thin
  details (hair strands, faces) soften. Hand-made overrides are the way to
  real redrawn art.
- A fade's tint multiplies where the game subtracts, so mid-fade art is a
  little brighter in its dark tones than the original pieces would be; the
  ends of the fade are exact.
- Pieces the game culls at the screen edge (tops above y = -32) are shown by
  replacement art: the whole frame is drawn and clipped by the screen.
- Tiny detached pixels in a few enemy frames (about 25 of 1,150 in rounds 3 and
  6) are the original art: sparks and fragments, several confirmed against
  direct captures. They are not extraction errors.
- Round changes and a second player joining cost a 0.5-1 s load.
- Output is 640x480 from 320x224: art is 1:1 horizontally and scaled 480/448
  vertically, like the planes. A two-texel gutter around packed frames stops
  the PowerVR sampling neighbours.

## History of decisions (why things are the way they are)

- `SORART01` ARGB1555 pages (3 MB for 149 frames, 25 s to read from CD) ->
  `02` 8-bit palette -> `03` zlib pages, because a 3.4 MB package embedded in
  the test ELF left the game no heap (`std::bad_alloc`) and 5.2 MB overran
  16 MB (Flycast: "Invalid load address", boot loop) -> `04` colour keys with
  masks, rounds, characters.
- The Dreamcast B button used to toggle a software comparison renderer (85 ms
  a frame, original graphics) in every build; it was pressed by accident and
  reported as "art resets and the frame rate collapses". It now needs
  `SOR_SOFTWARE_TOGGLE=1`. For any report of slowness read the run's log
  first (`build/logs/flycast.log`: `Renderer:`, `SLOW`, `HEARTBEAT`,
  `Enhanced art:` lines; `tools/flycast-speed.py` follows it live).
