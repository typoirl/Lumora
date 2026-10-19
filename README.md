# Lumora

A free, open source lighting console for Linux, macOS and Windows. Fixtures, groups and presets live in pools, cues are built from blueprints, and sequences play back on executors with a fader and a Go button. Two things are different on purpose:

- **MIDI learn on every executor.** Turn on *Learn MIDI*, click any fader or button, move a control on your MIDI device, done. Faders get soft takeover so they never jump, and controllers with motor faders or LEDs get feedback.
- **A cleaner interface.** One screen per job, color used only where it means something, and executor faders that glow with what's actually on stage.

![Lumora's Program view: groups, color presets and the fixture sheet](docs/screenshot.png)

Source: [github.com/typoirl/Lumora](https://github.com/typoirl/Lumora). Current version: a.7.2.1. The user guide is [docs/Lumora-Manual.pdf](docs/Lumora-Manual.pdf).

## User guide

**[docs/Lumora-Manual.pdf](docs/Lumora-Manual.pdf)** is the full user guide: a step-by-step first show for people who have never used a lighting console, then a chapter for every window and concept, a command line reference and a glossary. Every chapter is linked from the contents and the PDF bookmarks. In Lumora, **Show → User guide** or **F1** opens it; builds copy it next to the program.

The guide is written in `docs/manual/manual.md` and rebuilt for every version:

```sh
python3 docs/manual/build_manual.py                      # needs reportlab: pip install reportlab
python3 docs/manual/make_screenshots.py build/lumora     # optional: retake the screenshots
cmake --build build --target manual                      # same as the first line, from CMake
```

The version on the guide comes from `src/ui/main.cpp`, and a unit test fails when the guide (or its *What's new* entry) is behind the program.

## Status

Early but working (v0.7). The core engine, command line, outputs and MIDI layer are covered by unit tests, including a test that sends real Art-Net and sACN packets over the network loopback.

| Area | What works today |
| --- | --- |
| Fixtures | Pixel and multi-head fixtures with two levels of subfixtures (101.1, 101.1.2), pan and tilt in physical degrees with Flip and an XY pad, invert pan/tilt; built-in fixtures (generic, 260 Chauvet DJ and Chauvet Professional fixtures with gobo pictures for the moving heads, Pangolin BEYOND, Resolume Arena) and a browser for GDTF Share, the Open Fixture Library and QLC+, grouped by manufacturer; GDTF, OFL and QLC+ file import (16-bit channels, wheels, named ranges, gobo images), change type and mode after patching, virtual dimmer for RGB fixtures without one |
| Programming | Selection, programmer, encoders with an XY pad and a framing shutter view, color picker, live, layered presets (Universal, Global, Selective), 7 feature preset pools plus All |
| Plots | Sheets of fixtures and subfixtures showing intensity and color live (tap to select, drag a box), pool objects that work like their tiles, text, icons and SVG or picture backgrounds; stretchy truss; align and distribute, arrange, group, and link items to fixtures or pool objects; zoom and position kept per window in the view, lockable; hideable top bar; `Assign`/`Store … At Plot`; a Plots pool |
| Cues | Tracking sequences, fade and delay times, individual fade and delay times spread across the selection or through a blueprint line's spread, blueprints (group + presets + level + interleave), build blueprints into values |
| Playback | Sequence priorities (Low, LTP, HTP, Ultra; LTP for everything by default), Off when overridden, sequence colors, one sequence on several executors as one playback, Temp in and out with the cue and Off times; executors with Master / X-fade / Temp faders and one button: Go, GoBack, Pause, Flash, Temp, Toggle, On, Off, Swop, Select; pages; auto start/stop |
| Merging | Dimmer highest-takes-precedence, everything else latest-takes-precedence, programmer on top, highlight, grand master, blackout |
| Output | Up to 2048 universes over Art-Net and sACN (E1.31, multicast or unicast), plus Enttec DMX USB Pro and Open DMX. Only universes with patched fixtures are sent. |
| MIDI | Learn on any executor fader or button plus grand master, blackout, page and clear; pickup; invert; LED and motor feedback |
| Effects | Layered effects (dimmer, position, color, any attribute), wave/steps/form modes, shared custom shapes, preset points, laid out by pool spreads, relative layers that add to or take from what's underneath, stored in presets (with their spread, Universal / Global / Selective) and cues, crossfading with cue fades, 19-effect library, 10 rate masters (Rate, BPM, Ratio) with tap tempo, in a window of ten faders |
| Command line | Plain-English syntax, see below; macros with Follow times and Waits, foreground or background, popups, @ and variables |
| Shows | Single JSON file with fixture types embedded, so shows open on any machine |

## Building

You need CMake 3.21+, a C++17 compiler and Qt 6.2 or newer (Widgets, Svg, Network, SerialPort, Test).

**Linux (Debian/Ubuntu)**

```sh
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-svg-dev qt6-serialport-dev libasound2-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/lumora
```

**macOS**

```sh
brew install cmake ninja qt
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
open build/lumora.app
```

**Windows**

Install Qt 6 (with the Qt SVG and Serial Port modules) from the Qt online installer, then from a Qt developer command prompt:

```bat
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.7.0\mingw_64
cmake --build build --config Release
```

Run the tests with `ctest --test-dir build` or `./build/lumora_tests`.

### Building a Windows installer (MSI)

The installer files (`make-msi.bat`, `cmake/Packaging.cmake`, `cmake/MakeMsi.cmake` and the installer artwork) come with the release zips, not the GitHub repository; without them, CMake builds Lumora as usual and just has no `msi` target.

The quick way: double-click **`make-msi.bat`**. It finds Qt, MinGW, CMake and Ninja from the Qt installer (`C:\Qt`), installs the WiX Toolset 5 if it's missing (it needs the [.NET SDK](https://dot.net)), builds a Release and opens the folder with the `.msi`. Set `QT_DIR` first to pick a particular Qt (`set QT_DIR=C:\Qt\6.12.0\mingw_64`).

By hand: once, install the .NET SDK and the WiX Toolset:

```bat
dotnet tool install --global wix --version 5.0.2
```

Then, from a command prompt with Qt's CMake, Ninja and MinGW on the PATH (or the `msi` target in Qt Creator, with the Release configuration):

```bat
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target msi
```

The installer lands in `build-release/installer/Lumora-<version>-win64.msi`. It installs into Program Files with Start menu and desktop shortcuts and the user guide, and a newer MSI upgrades an older install in place. The `msi` target runs windeployqt (through Qt's deploy script), adds the MinGW runtime, installs the WiX UI extensions it needs, and runs CPack. Packaging lives in `cmake/Packaging.cmake` and `cmake/MakeMsi.cmake`; it needs CMake 3.30+ (recent Qt installs ship it).

WiX 7 and later refuse to run until their Open Source Maintenance Fee EULA is accepted (error WIX7015). Use WiX 5 as above, or accept it with `wix eula accept wix7` (or configure with `-DLUMORA_WIX_ACCEPT_EULA=ON`).

Lumora is in alpha, and versions are written `a.7.1.1` (the `a` stands for major version 0). The version is kept in one place, `setApplicationVersion` in `src/ui/main.cpp`; CMake, the user guide and the tests read it from there. Windows Installer only compares three numeric parts, so the MSI's version is `major.minor.(third×1000 + fourth)`: a.7.1.1 installs as 0.7.1001, which Windows lists under Installed apps.

On first launch Lumora opens a demo show: eight RGBW pars, four moving spots, groups, color/position/gobo presets, and three sequences on executors 201–203. Raise a fader to start it.

## How it works

### Views and windows

The rail on the left lists the show's **views**. They're stored in the show file: change the windows as much as you like, and pressing the view again brings back what was stored. A view remembers each window's own state too, such as which preset pool or color picker tab is showing, and recalling it brings that back. A changed view shows in italics with a dot. To keep the changes, press **Store** (the views light up red, like pool slots) and tap the view (or type `Store View 2`, or right-click it → Store the changes). New shows, including the demo, start with Program (groups, presets, fixture sheet), Sequences (sequence pool and cue editor), Effects (effects pool, rate masters and editor), Playback (executors) and Plot (the plots pool and a plot). Right-click a view to store, rename or delete it. A **Views** pool window (**+ → Views**) shows them as tiles: tap one to show it on that screen, or press Store and tap a slot to keep the windows on screen there (an empty slot makes a new view). **+ New view** makes an empty one; with Store on, it makes one from the windows on screen. On the command line: `View 2`, `Store View 5 "Busking"`, `Label View 2 "Name"`, `Move View 2 At 6`, `Copy View 2 At 7`, `Delete View 5`. **+** in the top bar (or a double-click on empty space) puts any window into the current view: Groups, Presets, Fixtures, Cues, Effects, Executors, Encoders, Plots, Plot, Macros, Color picker, Command keys, DMX monitor, Patch, DMX outputs or MIDI. Add as many as you like, including two of the same.

Windows sit on a grid. Drag a window's header to move it and drag any corner or edge to resize it; both snap to the grid, and windows never overlap. Every window is also a stack of tabs: drag a tab onto another window to stack it there, or onto empty space to give it a window of its own. **Show → Interface settings** sets the interface size (bigger for touch screens) and faders per executor page.

Below the views: **Patch**, **Outputs** and **MIDI** for setup.

### Pan and tilt in degrees

Pan and tilt are programmed in the fixture's physical degrees, with 0 in the middle of its range. A 540° pan runs from -270 to 270, and a 270° tilt from -135 to 135. Presets, cues, defaults and effects all keep degrees. A Universal position preset therefore points fixtures with different ranges the same way, and every fixture rests at 0°, 0°. The ranges come from the fixture file: GDTF physical ranges, Open Fixture Library angles and QLC+ pan/tilt maximums. Without one, pan is 540° and tilt is 270°. In the fixture editor, the **Degrees** column sets a channel's range (`-270 to 270`, or just `540`), and its default and highlight are in degrees too. On the command line, `Attribute Pan At -90` and `Attribute Tilt At 45` take degrees. Shows saved before this convert when they open.

**Flip** (the button next to the XY pad, or type `Flip`) points the selection the same way from the other side. Pan goes half a turn round, choosing whichever way stays nearer the middle of its range, and tilt mirrors. A fixture without room to go round is left alone.

### Encoders

The encoder strip runs along the bottom of the screen. Pick a category on the left (Dimmer, Position, Gobo, Color, Beam, Focus, Control). Then drag an encoder up or down or scroll it, double-click to type a value, or right-click for named slots (gobos with their pictures, colors, shutter modes) and Default. **Fine** (or holding Shift) makes small steps, **Default** puts the category's default values in the programmer for the selection (twice: every parameter), and ‹ › pages through more encoders than fit. Programmer values show in coral. Pan and tilt read in degrees, and the Position category has an XY pad. It shows every selected fixture as a dot, with pan across and tilt up and down, each across its own range. Drag the pad to move them together (Shift or **Fine** for small moves). **Flip** sits next to it. The encoders follow the output live, so a cue fading in or a fader moving turns them too. A page shows up to five encoders (Interface settings changes that), the arrow in the bar's corner tucks it away, and **+ → Encoders** adds them as a window with its own category and page. MIDI knobs and endless encoders map to the bar by position: the first encoder turns Pan on Position and Dimmer on Dimmer. Learn MIDI and click an encoder, or right-click it for **MIDI learn: encoder** and **MIDI learn: push** (the push opens its ranges). Hold Shift while turning for fine steps.

### Executor rows

Executors are numbered by row. Each kind looks and works differently:

- **101–199 are buttons.** Each has one key, Go unless you change it. A button always plays at full.
- **201–299 are faders.** Each has one button (Go by default) and a fader.
- **301–399 are encoders.** Each has a knob for the master and a push key (Toggle). Drag the knob up and down or scroll it; hold Shift for fine steps. The arc lights with what reaches the stage.
- **401–499 can be any of the three.** Set each one with right-click → **Executor type**, or type `Executor 401 Button` (or `Encoder`, `Fader`).

Right-click any executor to change its button, push or fader function, or to MIDI-learn it. An encoder's knob learns like a fader.

The standard Playback view stacks encoders 301–310 over faders 201–210, with keys 101–110 underneath. To get a row in any view, add an Executors window and right-click its tab to pick the row (101, 201, 301 or 401) and how many executors it shows. When several rows share a view, set **Page buttons → Hide** on all but one.

### Executors outside a Playback view

The grand master and blackout (**BO**) sit together at the top right; while either holds the output down, they pulse slowly.

When Store is on and the current view has no executor window, a panel takes the encoders' place with a tile for every executor in the 100, 200, 300 and 400 rows of the current page. Tap one to store onto it. With Learn MIDI on, the real faders and buttons appear there instead, so you can click the control to map.

### Keypad, keyboard shortcuts and tapping

Clicking anything that isn't a text field (a pool tile, a window, a button) ends typing in the command line or any other field, so the caret goes away and keyboard shortcuts work again. Enter in a pool's position box does the same.


The command line is always visible. **Keypad** (Ctrl+K) floats the on-screen command keys over the view; **+ → Command keys** puts them in a view as a regular window. Every key shows its keyboard shortcut underneath.

Single-key shortcuts: **S** Store, **U** Update, **Del** Delete, **R** Remove, **C** Copy, **M** Move, **L** Label, **⇧A** Assign, **F** Fixture, **G** Group, **P** Preset, **⇧S** Sequence, **Q** Cue, **E** Executor, **⇧P** Page, **⇧E** Effect, **Space** Go, **⇧Space** Go Back, **⇧G** Goto, **O** Off, **H** Highlight, **Page Down**/**Page Up** Next/Back through the selection, **Ctrl+L** Select last, **Alt+D/P/G/C/B/F/O/A** type a preset pool (`Color.` and so on), **T** Thru, **A** At, **⇧F** Full, **⇧Z** Zero, **⇧B** Blackout, digits, `.`, `+`, `-`, **Enter**, **Backspace**, **Esc** Clear, **⇧Esc** Clear All. They pause whenever you're typing in a text field (naming something, typing in the command line, a number box), and **Insert** turns them on and off (the **Shortcuts** button in the top bar shows which).

Commands finish by tapping. Press **Move**, tap a preset, then tap the slot it should go to: `Move Color.1 At Color.9`. The same works for Copy (groups, presets, effects, sequences), Delete, Update, Remove, **Label** (asks for the name) and **Assign** (tap a sequence, then an executor). Type `Assign Flash At` and tap an executor's button to give that button the function, or tap its fader or knob for a fader function (`Assign XFade At`). Moving something keeps the cues, blueprints and effects that use it pointing at it.

### Executor pages, grids and rows

Each executor row holds up to 60 per page (101–160, 201–260, 301–360, 401–460). Executor windows page like pools: ▲, the page number (type one to jump), ▼, and the page's name. **+ → Executor grid** lays executors out wide × tall like a pool (0 fits), from any executor; every executor window has the page control; **+ → Executor pages** is a pool of pages; **+ → Sequences** is the sequence pool on its own. **Show → Interface settings → Executors per row** sets the default width (1–60).

### Park

`Park` holds attributes where they are on stage, over playbacks, the programmer and Highlight, until `Unpark`. `Fixture 101 Park` (or `Park Fixture 101`) parks a whole fixture and its subfixtures, `Fixture 101 Intensity Park` one feature, `Attribute Pan Park` one attribute of the selection (click a knob's name on the encoder bar to type `Attribute Pan`). `Unpark All` releases everything. **+ → Park** lists what's parked, with **Unpark** buttons. Parked attributes are saved with the show.

### Effects

An effect is a stack of **layers**, each driving one part of a fixture: **Dimmer**, **Position**, **Color** or **Attributes** (zoom, iris, gobo rotation, anything you pick). Each layer moves in one of three ways:

| Mode | What it does |
| --- | --- |
| **Wave** | Between a low and a high point, following a **shape**: sine, ramps, triangle, square, pulse, breathe, random, smooth random, or one you draw |
| **Steps** | Through any number of points, with a crossfade (0 snaps, 100 fades the whole step), in order, there and back, or at random |
| **Form** | Position only: a circle, figure 8, pan or tilt line, square or triangle, around where each fixture points or around a position preset |

**Points can be presets.** A point is a level, a color, pan/tilt, or a reference to a preset, so an effect holds no fixture-specific data and runs on whatever you select. Edit the preset and the effect follows. **Shapes are shared too:** draw one with **Draw new…**, and editing it later updates every effect that uses it.

**Color layers drive every color system by default**: RGB, white, amber, UV, lime, CMY, CTO/CTB and color wheels (nearest named slot). Switch off any family per layer, say white or the wheel, to leave it alone.

How an effect rolls across the fixtures comes from **spreads** (below), not from the effect: recall a spread before running the effect, or give the blueprint line one. Without one, an effect keeps the layout it came with (the library's chases still chase). Each layer has its own speed multiplier and phase offset, so a color can roll at half the speed of a dimmer chase.

**Effects run in their own layer**, apart from values. Every sequence's values are combined first, by priority; effects then run on top of the winner, so a circle on one executor goes round a position from another and neither turns the other off. Effects compete only with effects (higher priority, then latest), so a Low color effect keeps running over an LTP color preset and a newer Low effect takes over from it. Calling a preset stomps the programmer's effects on those features.

### Spreads

Spreads change how the selection is used. **Every Nth** (interleave, from a starting fixture) decides which selected fixtures take values and effects. The layout then shapes how effects roll across them:
- **Phase** from–to, anywhere from -720 to 720: 0 to 0 moves everything together, 0 to 360 is a full chase, -360 to 0 runs it the other way, and 0 to 720 goes round twice.
- **Blocks**: neighbors share a phase.
- **Groups**: the phase repeats every N fixtures.
- **Wings**: the layout is mirrored.
- **Direction**: forward, backward, ends to middle, middle to ends or random.

The **Spreads** pool window (**+ → Spreads**) shows them as tiles. Tap one to put it on the selection (tap again to take it off), press Store and tap a slot to keep the active one, or right-click to edit or rename it. The header shows the active spread; tap it to change it, or press **Off**. The active spread goes with the selection: clearing the selection clears it, and the encoder bar shows it next to the selection count. Blueprint lines have a **Spread** column, so a cue can thin its group and lay out its effects from the pool, live. On the command line:
- `Spread 3` recalls one, and `Spread Off` turns it off.
- `Spread Every 2 Offset 2 Wings 2 Phase -360 Thru 0 Direction Backward` changes the active one.
- `Store Spread 3 "Evens"` keeps it in the pool.
- `Blueprint Cue 1 Group 2 Effect 1 Spread 3` uses one on a blueprint line.

The demo show starts with Odd, Even, Together, Wings 2, Blocks 2 and Middle out.

Using effects:

- **Effects pool:** tap an effect to run it on the selection. Right-click an empty slot to create one, blank or from the **library** (19 ready-made effects). Effects running in the programmer are listed under the pool with live **Rate** and **Size** sliders and **Stop**.
- **Effect editor:** edits layers and points with a live preview: one row per selected fixture across one cycle, with a playhead.
- **Presets:** storing a preset stores any running effect that drives that preset's feature, without fixtures. Applying the preset runs it on the new selection, so a Color preset can hold a color chase.
- **Cues:** storing a cue stores the running effects; blueprint lines can carry an effect on a group (`Blueprint Cue 4 Group 1 Effect 12`). The executor's fader scales an effect's intensity like any other value. Effects don't track yet: they run in the cue they're stored in.

Tap an effect in the Effects pool with nothing selected to open it in the effect editor; with fixtures selected, the tap runs it on them.

### Color picker

Four tabs: **Gels** (Rosco and Lee by number or name, filterable), **Standard** (named colors plus a hue/saturation field), **Temperature** (1800–10000 K; warm/cool-white fixtures blend their whites) and **Gamut** (click inside the LED gamut on a CIE 1931 diagram). Picks go to the selected fixtures using whatever they have: RGB(W) mixing, CMY, or the nearest color-wheel slot.

### Changing fixture types

In **Patch**, double-click a fixture's Type or Mode to pick a new one, or select several fixtures and press **Change type…** to change them together, optionally re-addressing them sequentially. Values for attributes the new type shares are kept.

### Storing

Press **Store**, then tap where it should go: a group slot (stores the selection), a preset slot (stores matching programmer values), a sequence slot, or an executor (stores the next cue; an empty executor gets a new sequence). `Esc` cancels.

Presets are layered. A preset can hold any of three layers, and the most specific one wins for each fixture:

| Layer | Holds | Wins over |
| --- | --- | --- |
| **Universal** | one value per attribute, for any fixture that has it | — |
| **Global** | one value per fixture type | Universal |
| **Selective** | one value per fixture | Global and Universal |

A store writes one layer and keeps the others. Dimmer and Color presets store **Universal** by default; Position, Gobo, Beam, Focus, Control and All store **Selective**. Add `/universal`, `/global` or `/selective` to store a different layer (`Store Color.3 /selective` gives a few fixtures their own version of a universal color), or right-click a preset and pick which layer to update. `/overwrite` replaces every layer. Each tile shows the layers it has (U, G, S). Cues keep references to presets: edit the preset and every cue that uses it changes.

The demo show starts with the standard colors as Universal color presets.

Gobo presets show the gobo picture on their tile when the fixture came from a GDTF file.

### Blueprints

A blueprint line says "take this group and apply these presets", optionally every Nth fixture. The level comes from a Dimmer preset like any other attribute; the demo show has universal Dimmer presets Full, 25% and Out, and new lines start at Full when there's a Full preset. Blueprint lines from older shows that had their own dimmer level get the matching universal Dimmer preset when the show opens (made if it doesn't exist yet). Blueprints are evaluated when the cue plays, so reordering a group or editing a preset updates the cue with no re-storing. **Build into values** (or `Build Cue 2`) turns a cue's blueprint into regular cue values, keeping the preset links. On the command line `BP` is short for `Blueprint`. Shows that called these recipes open as they were.

When something a blueprint line uses is deleted (a group, a preset, an effect), the line gets a red dot and its cue an orange one; hover for what's missing. The line keeps pointing at the old number, so storing a new group or preset there fixes it.

Each sequence ends with an **Off** line, greyed out. Only its fade and delay can be edited, and they're the times the playback fades out with when the executor is turned off (Off button or `Off Executor 201`). Intensity fades down and everything else holds until the fade ends. Letting go of a flash or pulling a fader to zero still stops at once.

**Top** in the Sequence editor jumps the list to the first cue and, if the sequence is on an executor, takes the playback back to cue 1 in its fade.

Blueprint-only cue lists work too: **New cue** in the Sequence editor adds a blank cue, `Store Cue 5` with an empty programmer stores an empty cue, and `Store Cue 1 Thru 40` lays out a whole run of cues at once. Each blueprint line picks its group from a dropdown, and its presets from a picker with a tab per preset pool plus **All presets**.

### Executors

Each executor has a fader and one button. Right-click an executor to assign a sequence, set its appearance (color), change the fader function (Master, X-fade, Temp), change what the button does, or start MIDI learn for one control. A sequence can be on several executors at once and they share one playback: Go on 101, Pause on 301, crossfade on 201 and Off on 401 all work the same cue list.

Sequences have a **priority**: Low, LTP (the default), HTP or Ultra. Everything is latest takes precedence at LTP, intensity included; a higher priority always wins; HTP sequences take the highest intensity among themselves; Ultra wins over the programmer too. **Off when overridden** (on by default) turns a sequence off once other sequences have taken all its values, so color sequences replace each other. Low sequences never turn others off. Page buttons above the executors switch pages; **+** adds the next page. Double-click (or right-click) a page button to name it, or type `Label Page 2 "Verse"`.

### MIDI learn

1. Plug in your controller. Lumora listens to every MIDI input by default (change that on the MIDI screen).
2. Turn on **Learn MIDI** in the top bar. Learnable controls get a dashed magenta outline.
3. Click a fader or button on any executor. It turns solid magenta while it listens.
4. Move a fader, turn a knob or press a key on your controller. It's mapped.
5. Repeat for other controls, then turn Learn MIDI off.

Notes, CC, pitch bend (14-bit faders) and program changes all work. Mappings follow the current page by default, so the same physical faders drive executors 201–210 on whichever page is active. A control can only do one thing; learning it again moves it.

**Pickup** (on by default for faders) means a physical fader only takes over once it passes the software fader's level, so nothing jumps when you change pages. If your controller has motor faders or LEDs, choose it as the *feedback output* on the MIDI screen: Lumora sends fader positions and button states back, and new fader mappings skip pickup.

### Command line

Presets are written by pool name: `Dimmer.1`, `Position.2`, `Gobo.3`, `Color.4`, `Beam.5`, `Focus.6`, `Control.7`, `All.8` (case doesn't matter, and `Preset 4.1` still works). Keywords are case-insensitive; common abbreviations work (`F`, `G`, `T`, `P`, `Seq`, `Exec`, `@`). A bare number means Fixture.

Selections add up until you clear them: `1`, `3`, `5`, `Group 2` selects all of them. Tapping fixtures in the sheet or tiles and tapping groups works the same way. Tap a selected fixture again (or Ctrl-tap a group) to take it out; **Clear** empties the selection.

```
Fixture 1 Thru 10 - 5 At 50        select and set dimmer
Attribute Pan At -90               pan and tilt in degrees (0 the middle of the range)
Flip                               the selection points the same way from the other side
Fixture 101.3 / Fixture 101 Cell 3   a subfixture (101.1 Thru 101.8, 101 Cell 1 Thru 4, 101.2.1 one level down)
Group 2 At Full                    select a group, full
At At                              the selection to full, right away (pressing At twice does it too)
At Color.3                         apply color preset 3 to the selection
Color.3                            the same (Dimmer, Position, Gobo, Color, Beam, Focus, Control, All)
Attribute Pan At 30                set any attribute by name
Store Group 3 "Front"              store the selection
Store Color.1 "Red" /overwrite     store programmer color values
Store Cue 5                        store into the selected sequence (an empty programmer makes an empty cue)
Store Cue 1 Thru 40                make cues 1 to 40 at once
Store Sequence 2 Cue 1.5           store into a specific sequence
Blueprint Cue 2 Group 1 Dimmer.1 Color.2 Every 2 Offset 2   (At 80 adds an 80% universal Dimmer preset)
Build Cue 2
Cue 3 Fade 4                       set times
Assign Sequence 2 At Executor 202
Assign Flash At Executor 201       the button's function (Go, GoBack, Pause, Flash, Temp, Toggle, On, Off, Swop, Select, Top)
Assign XFade At Executor 201       a fader's function (Master, XFade; Temp with "Fader" on the end)
Top Executor 201                   back to the first cue
Go Executor 201 / GoBack Executor 201 / Off Executor 201
Goto Cue 3 Executor 201
Executor 201 At 75                 set a fader
Label Color.1 "Deep red"           also Group, Sequence, Cue, Fixture
Label Group 3 [Enter]              no name: a dialog asks for one
Label [Enter]                      name the selected fixtures (numbered on, like the patch)
Delete Cue 3 / Delete Group 2 / Delete Color.1
Update                             merge the programmer into the live cue
Update Cue 3 / Update Color.1      merge into a specific cue or preset
Remove                             drop the selection's values from the programmer
Remove Fixture 3 Thru 5 Cue 2      drop fixtures from a stored cue
Copy Group 1 At 5 / Move Color.1 At Color.9 / Copy Cue 2 At 5 / Move Sequence 1 At 4
Effect 12                          run effect 12 on the selection
Effect 12 Rate 150 Size 50 Spread 180   adjust it (percent, percent, degrees)
Effect 12 Off / Effect Off         stop one effect / effects on the selection
Page 2 / Page + / Page -
Clear / ClearAll / Highlight / Blackout
Copy Color.1 Thru 10 At Color.15   copy (or move) a run of presets
Cue 3 Follow / Cue 3 Wait 5 / Cue 3 Go   a cue's trigger (Cue Off Follow: off after the last cue)
Next / Prev / All                  one fixture of the selection at a time (Page Down / Up), all again
Last                               the selection before the last Clear (Ctrl+L)
Park / Fixture 101 Park            hold everything on the selection / fixture 101 where it is
Fixture 101 Intensity Park         one feature (Position, Color…); Attribute Pan Park: one attribute
Unpark Fixture 101 / Unpark All
```

Preset types: 1 Dimmer, 2 Position, 3 Gobo, 4 Color, 5 Beam, 6 Focus, 7 Control, 8 All.

### Patching

The Patch screen has the patch on the left and **Add to patch** on the right. Its search covers every fixture at once: the types already in your library first, then GDTF Share, the Open Fixture Library and QLC+. Pick one that isn't in your library yet and Lumora downloads it on the spot; then choose the mode, quantity, first ID and address (the next free address is filled in) and press **Add to patch**. **Change type…** (or right-clicking a fixture's type) searches the same way.

**Invert pan** and **Invert tilt** in the patch are for fixtures hung the other way round: programming stays the same and the DMX runs backwards. Ticking one applies to every selected fixture.

Fixtures are laid out one after another, and a fixture never straddles two universes. When the next one doesn't fit in what's left of a universe, it starts the next universe at address 1. Adding fixtures, **Change type…** with new addresses, and **Patch** on unpatched fixtures all work this way, skipping addresses that other fixtures already use. The summary under **Add to patch** says where the fixtures will end up.

### Pixel fixtures

Fixtures with pixels (LED bars, battens, panels) are patched as one fixture with cells. The fixture keeps its ID, say 101, and holds the channels that act on the whole fixture, such as the master dimmer, strobe and control channels. Each pixel is a cell numbered from there: 101.1, 101.2 and up. On the command line, `Fixture 101.3` and `Fixture 101 Cell 3` are the same cell. `Fixture 101.1 Thru 101.8` (or `Thru 8`), `Fixture 101 Cell 1 Thru 4` and `Fixture 101 Thru 104 Cell 2` select ranges, and `+` and `-` work as usual. Cells are fixtures in every other way: store them in groups, give them presets, put them in cues, and run effects across them pixel by pixel. In the fixture sheet they nest under their fixture. The arrow in front of the ID (or a double-click on it) opens the fixture, and its cells are listed indented underneath with the same name. A fixture that is only pixels (12 RGB cells in a 36-channel mode, say) has no channels of its own, so its row is just a label over the cells. Selecting that row selects every cell. A closed fixture with some of its cells selected is tinted, so you can see they're in there. Which fixtures are open is kept with the view. A master dimmer rests at full, so the cells light up when you program them; the cells' own dimmers rest at 0. When the fixture has no channels of its own (an RGB bar with no master), `Fixture 101` selects all its cells. Subfixtures go two levels deep. On a fixture like an Acme Tornado, 101 is the whole fixture, 101.1 to 101.4 are its heads, and 101.1.1 and 101.1.2 are head 1's RGB ring and main beam. Each level nests in the fixture sheet with its own arrow. A head with no channels of its own is a label row for its parts. `101.1.1 Thru 101.4.1` selects the ring of every head, `101.1 Thru 4` selects the heads, and `Fixture 101 Cell 2.1` is head 2's ring. Cells come from Open Fixture Library matrices, GDTF (the heads are the sibling geometries that carry the same channels, whether written out one by one, like an Acme Tornado or a Chauvet COLORado PXL Curve, or repeated with geometry references, and the beams inside a head become its parts; a channel on the whole fixture stays on the fixture) and QLC+ heads. In the fixture editor, **Cells…** repeats one pixel's channels as many times as you need, and the Cell column takes `3` for subfixture 3 or `3.2` for the second part of head 3.

### Defaults

Every fixture has a default: where it rests when nothing is playing. Out of the box that's "open": white from RGB (or the open slot of a color wheel), open shutter and gobo, and pan and tilt centered, with the dimmer at zero. To change it, set the values (presets work, and stay references) and type `Store Default` (or press Store, then Default). `Store Default /overwrite` starts the defaults over; `Delete Default` removes the stored ones for the selection, or all of them.

**Default Default** puts the selection's default values into the programmer. Press the Default key twice (or D twice on the keyboard), or type `Default`. With nothing selected, or with `Default All`, it does every fixture. The encoder bar's **Default** button does the current category for the selection; press it again within a second and a half for every parameter. The keyboard's Home key works as Default too, and `Home` on the command line still means Default.

### Fixture editor

**Patch → Fixture editor…** builds a new fixture type, or edits the selected fixture's type (also on right-click of a fixture's type). For each mode you set the channels: DMX slot, an optional 16-bit fine slot, the parameter it controls, its name, default and highlight values, and its named ranges (gobo slots, colors, shutter modes; **Split into…** makes equal wheel slots). The **Cell** column marks a pixel's channels (empty: the whole fixture); select one pixel's channels and press **Cells…** to repeat them as cells after the last DMX slot. Channels come from the **Parameters** library: Dimmer, Pan and Tilt, Red, Green, Blue, White, Amber, UV, CMY, color and gobo wheels, shutter, prism, iris, frost, zoom, focus, control and more. Each one comes with a sensible default, highlight value and starting ranges. Every channel has a value range (**Min**, **Max**: 0–100 for a dimmer, -270–270 for a 540° pan, 5–50 for a zoom) and a DMX range (**DMX from**, **DMX to**: 0–255, or 0–65535 for 16-bit), and Default, Highlight and typed values (`Attribute Zoom At 30`) are in the channel's own values. An empty Highlight means Highlight leaves the channel alone. Double-click a parameter (or select several and press **Add**) to put it on the lowest free DMX slot, so a gap left by a removed channel is filled first; Pan and Tilt come 16-bit. **Add a group…** adds RGB, RGBW, RGBA, RGBAW, RGBWA+UV, CMY, pan/tilt, or the basics of a moving spot or wash in one go. **Custom channel** is still there for anything else.

Channels can be **virtual**: no DMX slot, but they're programmed, stored in cues and played back like any other. Every parameter is added with a DMX slot; clear the slot (or press **Virtual / DMX**) to make it virtual, and it stays virtual when you add more channels. A fixture with additive color (RGB and so on) and no dimmer gets a virtual Dimmer, which dims the colors the way a real dimmer would. That applies to fixtures from every library and to the ones you build. Adding a real Dimmer replaces it, and if you remove it, it doesn't come back. Other virtual channels don't output anything.

Saving writes a Lumora fixture file (`.lft`) to your fixture folder; an edited type replaces the original everywhere it's patched, and loads again next time.

### Screens

The **▣ 1** button at the top left (next to Show) opens into the screens 1–6. Pick a number to open another Lumora window (to put on a second monitor, say) or bring it to the front. Every screen recalls the show's views by itself, so screen 2 can show Playback while screen 1 shows Program; the setup pages, encoders and show file live on screen 1, and closing screen 1 closes the others. Screens that were open reopen next time. Keys go to the command line of the screen in front.

### Command history

**+ → Command history** lists every command run this session (typed, keyed or tapped) with its result, and the edits made in windows (stores, renames and so on). Double-click a command to put it back on the command line; right-click to run it again or copy lines. Its tab options filter it to commands or edits.

### Playback monitor

**+ → Playback monitor** is a cue list as it runs: the live cue is lit, the next one is marked, and a progress bar runs while a cue fades in. It follows the last sequence started with Go, or right-click its tab to lock it to one. The same options pick the columns (cue, label, fade, delay, notes, contents) and their order; you can also drag a column header. Double-click or right-click a cell to edit a cue's label, notes, fade or delay, or to go to that cue. Cue notes are also a column in the Sequence editor, and `Cue 3 Note "Wait for the actor"` sets them from the command line.

### Outputs

**Outputs** lists one row per output: the console universe, the protocol (Art-Net, sACN, Enttec USB Pro or Open DMX), the universe it's sent as on the network, the network card (or USB port) and an optional destination IP (empty broadcasts Art-Net and multicasts sACN). Edit any of them right in the row.

- **New universes get an output.** When a universe is patched for the first time, it gets an output with the defaults at the top: the protocol, the network card, and whether console universe 1 is Art-Net 0 or Art-Net 1. sACN universes match the console's.
- **Every output is on** unless you untick it. Removing one keeps it removed while its universe stays patched.
- **Double patching.** Select an output and use **Add output** to send that universe somewhere else too, for example Art-Net and sACN at once.
- **Network universe.** **Sent as** can be any universe: console universe 1 can go out as Art-Net 8.
- **USB DMX interfaces.** Enttec DMX USB Pro (and compatibles) and Enttec Open DMX (FTDI cables) still work: use **Add output → USB DMX interface**, or switch any output's protocol to one of them and pick its port. The line above the list shows which interfaces are plugged in; **Look again** after plugging one in.
- **More settings…** has the sACN priority.

The **DMX monitor** below shows the 512 channels of a universe. It follows the output you select.

### Window options

Right-click a window's tab for its options. Executors: how many to show, which numbers (the 101, 201, 301 or 401 row, or any first number). Fixtures: **Table** or **Tiles** layout, and which fixtures to show (all, patched, selected, or in the programmer). Drag a column heading in the fixture table to reorder the columns; the order is kept with the view. Drag the line at a heading's right edge to resize a column. The table has a Control column (lamp, reset, macros), and values show as preset names wherever they come from a preset: in the programmer, or from a blueprint line or preset reference in a running cue. While a cue is fading, or a master pulls the level down, you see the number instead. Pools (groups, presets, effects, views and sequence pools): tiles always fill the window. **Grid** sets tiles wide × tall (0 × 0 fits as many as there's room for at the chosen tile size). The top left of every pool has ▲, the slot in the top-left corner, and ▼. Type a number there to jump to it. The arrows and the mouse wheel move a row at a time and land on rows that start at 1: with 5 wide, going up from 7 goes to 1, and down goes to 11. **Lock position** keeps a pool where it is. The position is part of the view, like the other options. Options are saved per window, so two windows of the same kind can look different. A window's tab says what it shows: "Dimmer presets", "Faders 201–210", "Patched fixtures", "Sequence editor · Pars", "Effect editor · Chase".

Fixtures can be in the show without a DMX address, so you can program before the rig is patched. In **Add to patch**, clear the address field before adding; in the patch, clear a fixture's address (or universe), or right-click it and clear the address or choose **Unpatch** (with several fixtures selected, they all unpatch). Unpatched fixtures show "unpatched" in the patch and the fixture windows; select them, and store groups, presets and cues with them as usual, they just don't output. To patch them later, type an address in the patch, or right-click their Universe or Address cell and choose **Patch** (sequentially from the next free address in a universe).

Right-click a fixture's **ID** to give it a new one. With several fixtures selected, they're numbered on from the ID you type, and groups, presets and cues follow. Right-click the **Mode** cell to set the mode for every selected fixture of that type. Select several fixtures and right-click a **Name** to rename them all at once, numbered in ID order. When you add or rename fixtures, a name ending in a number counts on from it: "Spot 5" adds Spot 5, Spot 6, Spot 7 and so on. A name without a number starts at 1.

### Right-click to edit

Right-click anything you can change to open its editor. A pool tile (group, preset, effect, sequence) opens with its name ready to type, plus its other actions; an encoder opens its value and named ranges; a cell in the cue list, a blueprint line or the patch starts editing that cell, and a fixture's type opens Change type.

### Fixture library

**Patch → Fixture library…** browses three open fixture libraries and imports fixture types straight into Lumora:

- **GDTF Share**: the official GDTF library, mostly made by the manufacturers. Sign in with a free [gdtf-share.com](https://gdtf-share.com) account to list and download them. With **Stay signed in**, the session is kept between runs, and when it expires Lumora signs in again by itself. The password is kept on this computer, encrypted for your Windows account. **Sign out** forgets it.
- **Open Fixture Library**: the community library at [open-fixture-library.org](https://open-fixture-library.org). Multi-cell fixtures import as one fixture with numbered cells.
- **QLC+**: the [QLC+](https://www.qlcplus.org) fixture definitions, with their gobo images.

Search, filter by manufacturer, select one or many, and press **Import**. Imported fixture types go into the open show. Fixtures you already have are marked *In library*. The OFL and QLC+ lists ship with Lumora so you can browse offline, and **Refresh list** fetches the latest.

### Fixture files

Lumora reads **GDTF** (`.gdtf`), [Open Fixture Library](https://open-fixture-library.org) JSON and **QLC+** (`.qxf`) files. Use **Patch → Import file** to bring one into the open show (Lumora's own `.lft` files too). The files in the `fixtures/` folder next to Lumora are the library you pick from.

### What's saved where

A show file holds everything about the show: patch, fixture types (every type it patched, imported, downloaded or edited, so it opens the same on any computer), groups, presets, sequences, effects, executors, views and each window's state, the view on each screen, MIDI mappings, faders per page and whether keyboard shortcuts are on. Opening another show brings all of that with it.

Lumora keeps only this on the computer itself:
- **DMX outputs and their network interface.** The Art-Net, sACN and USB outputs, which network card each sends from, and the defaults for newly patched universes.
- **Window placement and screens.** Where windows sit, which screens are open, and the interface size, since these depend on the monitors.
- **The last show.** So it reopens at startup.
- **Library dialog details.** Your GDTF Share user name and the downloaded-file cache.

Shows saved before 0.7.0.4 carried their outputs. When you open one, Lumora uses those outputs only if this computer has none set up yet.

From GDTF, Lumora reads DMX modes, 8/16-bit channels, channel functions and named channel sets, wheels, and wheel slot images (gobos). Not yet supported: channels outside the first DMX break. `fixtures/gdtf/` has a sample GDTF spot (built by `tools/make_sample_gdtf.py`) that the demo show uses.

## Project layout

```
src/core/   show model, engine, outputs, MIDI, command line (Qt Core only, no GUI)
src/ui/     the Qt Widgets application
tests/      unit tests for the core
fixtures/   bundled fixture definitions (Open Fixture Library format)
third_party/rtmidi/   RtMidi for cross-platform MIDI
third_party/stb/      stb_image (zip decompression for GDTF)
resources/library/   bundled indexes of the Open Fixture Library and QLC+ fixture lists
docs/       the user guide PDF and its source (docs/manual/)
tools/      helper scripts (sample GDTF generator, library index builder)
```

The core has no GUI dependency, so a headless node, a web remote or an OSC bridge can be built on it.

## Roadmap

- GDTF DMX breaks
- Executor sheets
- Effects that track through cues, effect fade in/out, tap tempo and BPM sync
- 2D stage layout and a simple 3D visualizer
- OSC input, timecode, and remote control from a tablet
- Undo
- Pixel mapping media onto cells

## Contributing

Issues and pull requests are welcome. Please keep core changes covered by tests in `tests/test_core.cpp`, and keep the UI quiet: new color should mean something. Every version bump updates `docs/manual/manual.md` (including a *What's new* entry) and rebuilds the PDF.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE). RtMidi is included under its own MIT-style license in `third_party/rtmidi/LICENSE`; stb_image is public domain / MIT (`third_party/stb/LICENSE`).
