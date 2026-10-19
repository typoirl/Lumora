# Getting started

## Welcome to Lumora {#welcome}

Lumora is a lighting console for Windows: patch your rig, build looks in the programmer, store them as cues and play them back on executors, over Art-Net, sACN or a USB DMX interface.

This guide assumes you know lighting and DMX, but not any particular console.

- **New to Lumora?** Read [How Lumora works](#basics), then follow [Your first show](#first-show) step by step. Terms are explained where they first appear, and again in the [Glossary](#glossary).
- **Looking something up?** The [Command line reference](#command-reference) and [Keyboard shortcuts](#shortcuts) chapters are the quick references.

### How this guide is organized

1. **Getting started**: how Lumora works, installing and updating it, a tour of the screen, and a complete first show.
2. **Setting up**: patching fixtures, fixture types, subfixtures (pixels and multi-head fixtures), outputs and MIDI.
3. **Programming**: selecting fixtures, the programmer, encoders, color, groups, presets, spreads and effects.
4. **Cues and playback**: sequences, cues, blueprints, executors, and watching a show run.
5. **The workspace**: views, windows, screens, and the windows you can add.
6. **Reference**: the command line, shortcuts, show files, troubleshooting and a glossary.

Every entry in the table of contents is a link: click it to jump to that chapter. Links inside the text work the same way.

> **Tip:** Lumora opens with a ready-made *demo show* the first time you start it. It has lights, groups, presets and cues already made, so you can try anything in this guide on it without breaking anything you care about. **Show → New demo show** brings it back at any time.

### Conventions

- **Bold** words are buttons, menu items and window names you can click, such as **Store** or **Show → Save**.
- `Monospaced` text is something you type on the command line, such as `Fixture 1 Thru 4 At 50`. Press **Enter** after it.
- Keyboard keys are written like **Esc** or **Shift+A**.
- *Tap* means click with the mouse or touch on a touch screen.

## How Lumora works {#basics}

A few ideas everything else in Lumora is built on.

### Features and attributes

Every controllable thing on a fixture is an **attribute** (Dimmer, Pan, Red, Gobo1, Zoom…). Attributes are grouped into seven **features**, which is how the encoders, presets and the fixture sheet are organized:

| Feature | What it covers |
| --- | --- |
| **Dimmer** | Intensity: how bright the light is. |
| **Position** | Where a moving light points: pan (left/right) and tilt (up/down). |
| **Gobo** | Gobo wheels: patterns that shape the beam. |
| **Color** | Color mixing (red, green, blue, white, amber, cyan, magenta, yellow…), color wheels and color temperature. |
| **Beam** | Shutter and strobe, iris, prism, frost. |
| **Focus** | Zoom and focus. |
| **Control** | Lamp on/off, reset, fans, built-in macros. |

### IDs and addresses

Every fixture has an **ID**, the number you select it by (`Fixture 11`), independent of its DMX address. Pixels and heads of a fixture are **subfixtures** with IDs like 101.1 (see [Subfixtures](#subfixtures)). Addresses are written *universe.address*: `2.051` is universe 2, channel 51. Lumora reads fixture types from GDTF, Open Fixture Library and QLC+ files (see [Fixture types](#fixture-types)).

### The programmer, presets and cues

Working a console has three layers:

1. **The programmer** is your scratch pad. You select fixtures and set their values; what's in the programmer goes straight to the stage, on top of everything else, until you clear it.
2. **Presets** are named values you reuse: "Red", "Downstage center", "Star gobo". Presets are referenced, not copied, so when you change a preset every cue that uses it changes too.
3. **Cues** are stored looks. A list of cues is a **sequence**. During the show you press **Go** to move from one cue to the next, with smooth fades.

Sequences play back on **executors**: on-screen faders and buttons (and real ones, if you connect a MIDI controller).

### Tracking

Lumora sequences **track** by default. A cue only stores what *changed*. If cue 1 turns the stage blue and cue 2 only raises the backlight, the stage stays blue in cue 2 because the blue *tracks* through. This makes editing easy: change the color in cue 1 and every following cue that didn't change the color follows along. You can switch tracking off per sequence.

### Precedence: who wins when two things control the same light

When several sequences play at once, each sequence's **priority** decides (see [Priority](#priority)):

- **By default everything is LTP (latest takes precedence), intensity included**. The sequence that went most recently wins, for intensity, color, position and the rest.
- **A higher priority always wins:** Low, then LTP, then HTP, then Ultra. Among **HTP** sequences intensity is highest takes precedence (40% and 70% make 70%).
- **The programmer wins over every sequence except Ultra ones.**
- A sequence whose values have all been taken over by others **turns itself off** (Off when overridden), so ten color sequences replace each other instead of piling up.

## Installing and starting Lumora {#install}

### Installing

Lumora comes as a Windows installer, `Lumora-<version>-win64.msi`.

1. Double-click the `.msi` file.
2. If Windows says it "protected your PC", click **More info**, then **Run anyway**. (The installer isn't signed by a known publisher yet.)
3. Accept the license, keep or change the install folder, and click **Install**. Windows asks for permission to install.
4. Start Lumora from the **Lumora** shortcut on the desktop or in the Start menu.

Lumora installs into *Program Files\Lumora*. The Start menu also has **Lumora User Guide**, which opens this guide.

> **Note:** The first time Lumora sends Art-Net or sACN, Windows Firewall may ask whether to allow it on your networks. Allow it on the network your DMX nodes are on, or nothing reaches them.

### Updating

Run the installer for the new version. It replaces the old version in place: there's no need to uninstall first. Your shows, settings (outputs, interface, MIDI mappings) and downloaded fixture types are kept, because they live in your own user folders rather than in Program Files.

Check which version you have under **Show → User guide** (the cover page) or in Windows' **Settings → Apps → Installed apps**.

### Uninstalling

Open Windows' **Settings → Apps → Installed apps**, find **Lumora**, and choose **Uninstall**. Your show files are left where you saved them.

### First launch

The first time it starts, Lumora opens the **demo show**: eight RGBW LED pars, four moving spots, two pixel bars, with groups, presets, effects and three sequences ready on executors 201–203. Go to the **Playback** view and raise fader 201 to see it run.

After that, Lumora reopens the last show you used.

### Opening the guide

**Show → User guide** (or **F1**) opens this guide, and so does **Lumora User Guide** in the Start menu.

## A tour of the screen {#tour}

![The Program view of the demo show, with eight pars selected](images/overview.png)

The Lumora window has six areas.

### The top bar

![](images/top-bar.png)

From left to right:

- **Show**: the show menu (new, open, save, views, interface settings, the user guide, quit).
- **▣ 1**: screens. Open more Lumora windows for other monitors; see [Screens](#screens).
- **The show name**. A dot after it means there are unsaved changes.
- **Store**: arms Store. The next thing you tap (or type) is where something gets stored. See [Storing](#storing).
- **Learn MIDI**: maps controls on a MIDI controller to on-screen faders and buttons. See [MIDI](#midi).
- **Highlight**: makes the selected fixtures bright and open so you can see which lights they are.
- **Clear**: clears the selection, then (pressed again) the programmer.
- **Keypad**: floats the on-screen command keys over the window.
- **Shortcuts**: turns keyboard shortcuts on and off (lit when on).
- **+**: adds a window to the current view.
- **GM**: the grand master, which scales all intensity. **BO** is blackout. Both pulse while they hold the output down.
- The frame rate the engine is running at.

### The rail

![](images/rail.png)

The column on the left lists the show's **views**: saved screen layouts such as Program, Sequences, Effects, Playback and Plot. Tap one to switch. **+ New view** makes another. Below them, **Setup** has **Patch**, **Outputs** and **MIDI**. See [Views and windows](#views).

### The windows

The big area in the middle shows the windows of the current view: pools of groups and presets, the fixture sheet, cue lists, executors and so on. Windows can be moved, resized, stacked as tabs and given options (right-click a window's tab).

### The encoder bar

The strip along the bottom is the **encoder bar**: virtual knobs for the selected fixtures' attributes, organized by feature. It's on the main screen (screen 1) only; other screens use that space for more window grid. The small arrow in its top-right corner tucks it away to a thin strip (and brings it back), and an **Encoders** window (**+ → Encoders**) puts encoders anywhere else. See [Encoders](#encoders).

### The command line

![](images/command-line.png)

The bottom line is the **command line**. Type commands such as `Fixture 1 Thru 4 At Full` and press **Enter**. The area to its right shows what the last command did. Everything you can do with the mouse you can also type; see the [Command line reference](#command-reference).

### Right-click to edit

Almost anything you can change opens a small editor when you right-click it: a pool tile opens with its name ready to type plus its actions, an encoder opens its value and named ranges, a cell in a table starts editing. When in doubt, right-click.

## Your first show {#first-show}

This tutorial takes you from an empty show to a short cue list you can play back. It uses fixtures from Lumora's built-in library, so you can follow it without any lights connected: the fixture sheet and DMX monitor show what would be sent.

### Step 1: Start a new show

1. Click **Show → New show**. Lumora asks whether to save the current show if it has changes.
2. The new show has five empty views (Program, Sequences, Effects, Playback, Plot) and no fixtures.
3. Click **Show → Save as…** and give it a name, so you can save as you go with **Show → Save** (or **Ctrl+S**).

### Step 2: Patch some fixtures

You'll add four RGBW LED pars and two moving spots.

![The Patch screen, with Add to patch on the right](images/patch.png)

1. Click **Patch** in the rail.
2. In **Add to patch** on the right, type `RGBW` in the search box and click **RGBW LED Par** in the list.
3. Set **Mode** to *6-channel*, **Quantity** to 4 and **First ID** to 1. Leave **Name** empty (the fixtures will be called Par 1, Par 2…).
4. **Address** shows `1` (universe) and the next free address. Leave it.
5. Read the summary line: "IDs 1–4, 6 channels each, 1.001 to 1.024". Click **Add to patch**.
6. Search for `Moving Head Spot`, pick it, choose the *16-channel* mode, Quantity 2, First ID 11, and click **Add to patch**. Lumora fills in the next free address (1.025).

The patch table on the left now lists your six fixtures with their IDs, types, modes and addresses.

> **Tip:** The **ID** is the number you use to talk to a fixture (`Fixture 11`). It's independent from the DMX address, so you can number fixtures in a way that makes sense on stage (say 1–10 for front light, 21–30 for back light) whatever their addresses.

### Step 3: Check the output

1. Click **Outputs** in the rail. Universe 1 already has an output: Lumora adds one for every universe you patch, sending Art-Net on any network card.
2. If you have a network DMX node, set its network card under **Network interface**, and make sure the node listens to the same Art-Net universe (the **Sent as** column). See [Outputs](#outputs).
3. The **DMX monitor** at the bottom shows the 512 channels of the universe. Keep it in mind: it's the best way to check that Lumora is sending what you expect.

### Step 4: Select fixtures and set intensity

1. Click **Program** in the rail.
2. Click in the command line, type `Fixture 1 Thru 4` and press **Enter**. The four pars highlight in the fixture sheet and the encoder bar shows "4 fixtures selected".
3. Type `At 75` and press **Enter**. The pars go to 75%. In the fixture sheet their Dimmer column reads 75 in coral: coral means *in the programmer*.
4. Try the encoder: drag the **Dimmer** knob up or down, or scroll over it.

You can also select by clicking rows in the fixture sheet. Selections **add up**: clicking another row (or typing another fixture) adds it to what's already selected. Press **Clear** (or **Esc**) to start a new selection.

### Step 5: Make a group

A group is a saved selection, so you don't have to type the fixture numbers every time.

1. With fixtures 1–4 selected, press **Store** in the top bar. The empty slots in the pools light up.
2. Tap slot 1 in the **Groups** pool. The group is stored.
3. Right-click the new tile, type `Pars` in **Name** and press **Enter**.
4. Press **Clear**, select the spots with `Fixture 11 Thru 12`, press **Store** and tap group slot 2. Name it `Spots`.

Now tapping the **Pars** tile selects the four pars.

### Step 6: Make color presets

1. Tap the **Pars** group (clear first if anything else is selected).
2. Make sure the Color tab of the presets pool is showing (tabs along its top: Dimmer, Position, Gobo, Color…).
3. Click the **Color** button in the encoder bar and set **Red** to 100, **Green** to 0, **Blue** to 0 and **White** to 0 (drag the knobs, or double-click one to type a value).
4. Press **Store** and tap an empty slot in the Color presets pool. Right-click it and name it `My red`.
5. Set the encoders to blue (Red 0, Green 0, Blue 100) and store another preset, `My blue`.

The new preset tiles show a color swatch. Tapping a preset tile applies it to the selected fixtures.

> **Tip:** Picking a color in the [color picker](#color) instead (a gel, a standard color or a color temperature) and then storing names the preset for you, for example "Red" or "L201 Full CT Blue".

> **Note:** New shows don't come with color presets, but the demo show does: the thirteen standard colors plus a 3200K warm white in the Color pool. Using a preset rather than raw values in cues means you can later change "My red" once and every cue follows.

### Step 7: Store your first cue

1. Press **Clear** twice (once clears the selection, the second time empties the programmer).
2. Tap **Pars**, type `At 80` and press **Enter**, then tap **My blue**.
3. Press **Store**. The executor tiles appear along the bottom (the encoder bar's place) because this view has no executors.
4. Tap executor **201**. Lumora makes a new sequence, stores cue 1 in it and assigns it to executor 201.
5. Press **Clear** twice.

### Step 8: Store more cues

1. Tap **Pars** and **My red**. Tap **Spots**, type `At Full`.
2. Press **Store** and tap executor 201 again. Cue 2 is stored after cue 1.
3. Clear twice. Tap **Pars**, type `At 0`. Store onto 201 again: cue 3 takes the pars out but leaves the spots up (tracking).
4. Clear twice.

### Step 9: Play it back

![The Playback view with sequence "Pars" running on executor 201](images/executors.png)

1. Click **Playback** in the rail. Executor 201 shows your sequence.
2. Raise fader 201 (drag it up). Raising a fader from zero starts its sequence at cue 1.
3. Press **Go** on executor 201 to move to cue 2, and again for cue 3. Each cue fades in over its fade time (2 seconds unless you change it).
4. **Pause** holds a fade; **Flash** brings the sequence to full while you hold it.
5. Pull the fader down to stop the sequence.

### Step 10: Adjust the cues

1. Click **Sequences** in the rail. The sequence pool is on the left and its cues on the right.
2. Double-click a cue's **Name** cell and type a name, such as `Blue look`.
3. Double-click the **Fade** cell of cue 2 and type `5` for a five-second fade.
4. Save the show with **Show → Save**.

You've patched fixtures, made groups and presets, stored cues and played them back. The rest of this guide explains each part in depth.

# Setting up

## Patch {#patch}

![The Patch screen](images/patch.png)

The Patch screen (**Patch** in the rail) lists every fixture in the show on the left and has **Add to patch** on the right.

### The patch table

| Column | What it is | How to change it |
| --- | --- | --- |
| **ID** | The fixture's number, used to select it. | Right-click → give it a new ID. With several fixtures selected they're numbered on from the ID you type. Groups, presets and cues follow. |
| **Name** | A name for your own use. | Double-click to edit. Select several and right-click a name to rename them all at once, numbered in ID order. |
| **Type** | The fixture type. | Double-click or right-click → **Change type or mode…** |
| **Universe**, **Address** | Where it sits in DMX. | Type a new value. Clear either to unpatch the fixture. |
| **Invert pan**, **Invert tilt** | For fixtures hung the other way round. | Tick or untick; applies to every selected fixture. |

### Adding fixtures

1. Type part of the fixture's name or manufacturer in the search box. The list shows types already in your library first, then (after a moment) matches from GDTF Share, the Open Fixture Library and QLC+. **All sources** limits the search to one of them.
2. Click the fixture. If it isn't in your library yet, Lumora downloads it.
3. Choose the **Mode**, the **Quantity**, the **First ID** and a **Name** (optional).
4. **Address** has the universe and the start address. Lumora fills in the next free address. Clear the address field to add the fixtures *unpatched* (see below).
5. Check the summary line, then click **Add to patch**.

**Naming.** A name ending in a number counts on from it: "Spot 5" adds Spot 5, Spot 6, Spot 7… A name without a number starts at 1. An empty name uses the fixture type's short name.

**Addresses never straddle universes.** Fixtures are laid out one after another from the start address, skipping addresses other fixtures already use. When the next fixture doesn't fit in what's left of a universe, it starts the next universe at address 1. The summary says where the fixtures will end up, including when they carry on into the next universe.

### Unpatched fixtures

A fixture can be in the show without a DMX address. You can select it, store groups, presets and cues with it, and program the whole show before the rig is hung; it just doesn't output. To patch it later, type an address in the table, or right-click its Universe or Address cell and choose **Patch** (sequentially from the next free address in a universe). To unpatch, clear its address or right-click → **Unpatch** (with several fixtures selected, they all unpatch).

### Changing a fixture's type or mode

Select one or more fixtures and press **Change type…** (or right-click a fixture's type). Search for the new type, pick the mode, and optionally re-address them sequentially from a start address. Values for attributes the new type shares are kept in presets and cues.

### Removing fixtures

Select them and press **Remove**. They disappear from groups, presets, cues and the programmer.

### Invert pan and tilt

A moving head hung upside down, or turned round, moves the opposite way. Tick **Invert pan** or **Invert tilt** and the DMX for that axis runs backwards. You keep programming in the same degrees, so presets work on every fixture.

### The fixture library and fixture files

- **Fixture library…** browses GDTF Share, the Open Fixture Library and QLC+, and imports fixture types into the show. See [Fixture types](#fixture-types).
- **Import file…** loads a GDTF (`.gdtf`), Open Fixture Library (`.json`), QLC+ (`.qxf`) or Lumora (`.lft`) file into the show.
- **Fixture editor…** builds a new fixture type or edits one. See [The fixture editor](#fixture-editor).

## Fixture types {#fixture-types}

A fixture type describes a fixture: its manufacturer and name, its modes, and for each mode its channels. Every type a show uses is saved inside the show file, so a show opens the same on any computer.

### Where fixture types come from

| Source | What it is |
| --- | --- |
| **GDTF Share** | The official GDTF library, mostly written by the manufacturers. Needs a free account at gdtf-share.com. |
| **Open Fixture Library** | A community library at open-fixture-library.org. |
| **QLC+** | The fixture definitions from the QLC+ project, with gobo images. |
| **Lumora's library** | The files in the `fixtures` folder next to Lumora: a few generic fixtures. |
| **Your own** | Types built in the fixture editor. |

### The Fixture library window

**Patch → Fixture library…** opens a browser for Lumora's own fixtures and all three online libraries.

1. Choose a source tab: **Built in**, **GDTF Share**, **Open Fixture Library** or **QLC+**.
2. Fixtures are grouped by manufacturer. Open a manufacturer with the arrow beside it to see its fixtures. As soon as you type in the search box, every manufacturer with a match opens, showing only the matches ("6 of 43 fixtures"); clear the search and they close again. The manufacturer list on the right narrows to one maker. Fixtures already in your library are marked *In library*.
3. Select one or many and press **Import**.

![The Open Fixture Library tab, searching "spot": each manufacturer with a match opens](images/fixture-library.png)

![The Built in tab: Lumora's own fixtures, grouped by manufacturer](images/fixture-library-builtin.png)

**Built in** lists the fixture types that come with Lumora: generic dimmers, pars, a pixel bar, moving heads and a profile with framing shutters; the **Chauvet DJ** (134) and **Chauvet Professional** (126) ranges, with every DMX mode charted in their manuals and, for 25 of the moving heads, the gobo pictures from Chauvet's own GDTF files on the wheel slots; and media and laser servers:

| Fixture | Modes | Notes |
| --- | --- | --- |
| **Pangolin BEYOND DMX Server (FB3)** | Basic 4 ch, Standard 8 ch, Extended 12 ch, Full 16 ch | The classic FB3 map: Access, Page, Cue, Speed, Brightness, Zoom, Size, Angle, Position, Visible points, Scan rate, Color slider. The Access channel rests at the value for the mode's channel count. |
| **Pangolin BEYOND DMX Server** | Mid 32 ch, Max 50 ch | Every page and cue, 16-bit size, angle, position and zoom, RGB recolor, four FX (with action and speed in Max) and parameters. |
| **Resolume Arena Composition** | Auto map 27 ch | Master, crossfader, column trigger, dashboard dials, BPM, tap, speed and transform. Patch it at channel 1. |
| **Resolume Arena Layer** | Auto map 38 ch | AV fader, clip trigger, dashboard dials, playback, opacity, blend mode and transform. Layer *n* starts at channel 28 + 38 × (*n* − 1): layer 1 at 28, layer 2 at 66, layer 3 at 104. |

Set BEYOND's DMX Server to the matching mode, and load Resolume's DMX Auto Map shortcut preset in Arena (Preferences → DMX), so the channels line up. Built-in types are always there to patch; **Import** on the Built in tab copies one into the show so you can edit it.

For GDTF Share, sign in with your gdtf-share.com account. With **Stay signed in**, the session is kept between runs, and Lumora signs in again by itself when it expires; the password is stored encrypted on this computer. **Sign out** forgets it. The OFL and QLC+ lists ship with Lumora so you can browse offline; **Refresh list** fetches the latest.

### What Lumora reads from fixture files

- DMX modes, 8- and 16-bit channels (coarse and fine).
- Channel functions and named ranges: color wheel slots, gobos, shutter modes and so on.
- Gobo and color wheel images (GDTF and QLC+).
- Physical ranges of pan and tilt in degrees (see [Pan and tilt in degrees](#degrees)).
- Subfixtures: pixels and heads (see [Subfixtures](#subfixtures)).

### Virtual dimmers

Many RGB LED fixtures have no dimmer channel: their brightness is the color channels themselves. Lumora gives such fixtures a **virtual Dimmer**, an intensity channel that isn't sent as DMX but scales the color channels. You program intensity the same way on every fixture, and a color preset doesn't need to know about brightness.

### Default values

Every attribute has a *default*: where it rests when nothing is playing. Lumora sets defaults to "open": white from RGB (or the open slot of a color wheel), open shutter and gobo, pan and tilt in the middle, dimmer at zero. You can store your own defaults per fixture; see [Defaults](#defaults).

## The fixture editor {#fixture-editor}

![The fixture editor, editing a moving spot](images/fixture-editor.png)

**Patch → Fixture editor…** builds a new fixture type, or edits the selected fixture's type (also on right-click of a fixture's type → **Edit this fixture type…**). Saving changes the type in this show, everywhere it's patched.

### Layout

- **Manufacturer**, **Name**, **Short name** at the top. The short name names newly patched fixtures.
- **Modes** on the left: **Add**, **Copy** (a mode as a starting point), **Rename** and **Remove**.
- **Parameters** library: every standard attribute with a sensible default, highlight value and starting ranges.
- **Channels** of the selected mode, in the middle.
- **Ranges** of the selected channel, on the right.

### Channels

| Column | Meaning |
| --- | --- |
| **Fine** | The slot of the 16-bit fine channel, if any. |
| **Cell** | For pixel and multi-head fixtures: which subfixture the channel belongs to. Empty is the whole fixture; `3` is subfixture 3; `3.2` is the second part of head 3. |
| **Parameter** | The attribute it controls (double-click for a list). |
| **Name** | The name shown for it. |
| **Default** | Where the channel rests, in its own values: 0 for a dimmer at zero, 0° for pan in the middle. |
| **Highlight** | Where **Highlight** puts it. Leave it empty (right-click → delete, or clear the cell) and Highlight leaves the channel alone. |
| **Min**, **Max** | The **value range**: the channel's value at each end of its DMX range. A dimmer is 0 to 100 (%); a 540° pan is -270 to 270, 0 the middle; a zoom might be 5 to 50 (°). Put the bigger number in **Min** for a channel that runs the other way. |
| **DMX from**, **DMX to** | The **DMX range** the value range covers: 0 to 255, or 0 to 65535 for a 16-bit channel (one with a **Fine** slot). Narrow it when only part of the channel does the job, such as a dimmer that's full at 200. |
| **Ranges** | How many named ranges it has. |

Default and Highlight are always in the channel's own values (degrees, %, or whatever its value range says), never raw DMX. The encoders show the same values.

### Adding channels

- **Double-click a parameter** (or select several and press **Add**) to put it on the lowest free DMX slot. A gap left by a removed channel is filled first. Pan and tilt come 16-bit.
- **Add a group…** adds a set in one go: RGB, RGBW, RGBA, RGBAW, RGBWA+UV, warm/cool white, CMY, pan/tilt, or the basics of a moving spot or wash.
- **Custom channel** adds a channel of your own after the selected one.
- **Virtual / DMX** makes the selected channel virtual or gives a virtual one the next free slot.
- **↑ ↓** reorder the list; **Number in order** renumbers the DMX slots in list order.

The parameter library has the attributes fixtures commonly use: besides dimmer, pan/tilt, RGBW and CMY, there are **infinite pan** and **infinite tilt**, pan and tilt speed, movement macros (with speed and size), more emitters (**Orange**, **Mint**, **Purple**, additive **Cyan**, **Magenta** and **Yellow**, Lime, Indigo, warm and cool white, UV), hue/saturation/brightness for virtual color, red shift, color macro speed, wheel shake, a third gobo wheel and animation rotation, **Prism 2** and its rotation, **Frost 2**, virtual strobe and strobe duration, effect speed, **framing blades** (in and swivel for 1–4, and blade rotation), auto focus, and control channels for haze, bubbles, dimmer speed mode, operating mode, program speed, motors and laser patterns.

### Editing several channels at once

Select several channels (Shift- or Ctrl-click), then right-click a cell in one of them: the editor sets that column for all of them. Give red, green, blue and dimmer the same **Cell** in one go, rename them, or set the same default, highlight, value range or DMX range. DMX slots and parameters are still set one by one.

### Ranges

Ranges name parts of a channel's DMX range: color wheel slots, gobos, shutter modes. They're in DMX values: 0–255, or 0–65535 for a 16-bit channel. **Add range** adds one after the last; **Split into…** replaces them with equal steps (for a wheel with 8 slots, say). Named ranges show on the encoders and in the fixture sheet.

### Cells (pixel fixtures)

To build a pixel bar: add one pixel's channels (for example an RGB group), select those rows, press **Cells…** and enter how many cells. The selected channels become cell 1 and are copied for the others after the last DMX slot. Channels without a cell number belong to the whole fixture (a master dimmer or strobe). A master Dimmer rests at full so the cells light up when programmed.

### Saving

**Save fixture type** (or **Create fixture type**) checks the type and saves it into the show. Problems, such as two channels on the same slot, show in red at the bottom and must be fixed first.

## Subfixtures: pixels and multi-head fixtures {#subfixtures}

Some fixtures are several lights in one: an LED bar with 8 separately controllable pixels, a panel of 25 cells, a fixture with four moving heads. Lumora patches these as **one fixture with subfixtures**.

### Numbering

The fixture keeps its ID, say **101**, and holds the channels that act on the whole fixture: a master dimmer, strobe, control. Each subfixture is numbered from it: **101.1**, **101.2** and up.

Subfixtures go **two levels deep**. On a fixture with four heads, each with an RGB ring and a main beam:

- **101** is the whole fixture (master dimmer, shutter).
- **101.1** to **101.4** are the heads (each with its own pan and tilt).
- **101.1.1** and **101.1.2** are head 1's ring and main beam.

### Selecting subfixtures

| Command | Selects |
| --- | --- |
| `Fixture 101.3` or `Fixture 101 Cell 3` | Cell 3 of fixture 101. |
| `Fixture 101.1 Thru 101.8` or `Fixture 101.1 Thru 8` | Cells 1 to 8. |
| `Fixture 101 Cell 1 Thru 4` | Cells 1 to 4. |
| `Fixture 101 Thru 104 Cell 2` | Cell 2 of fixtures 101, 102, 103 and 104. |
| `Fixture 101.1.1 Thru 101.4.1` | The ring of every head. |
| `Fixture 101 Cell 2.1` | Head 2's ring. |
| `Fixture 101` | The fixture itself, or every subfixture when it has no channels of its own. |

Subfixtures are fixtures in every other way: store them in groups, give them presets, put them in cues, and run effects across them pixel by pixel.

### In the fixture sheet

![Subfixtures nested in the fixture sheet](images/fixture-sheet.png)

Subfixtures nest under their fixture. Click the arrow in front of an ID (or double-click the ID) to open or close a fixture. A fixture that is only pixels has no channels of its own, so its row is just a label over the cells; selecting that row selects every cell. A closed fixture with some of its cells selected is tinted amber, so you can see they're in there.

### Defaults

A master dimmer (on the whole fixture) rests at full, so the cells light up as soon as you program them. Each subfixture's own dimmer rests at 0.

### Where subfixtures come from

- **GDTF**: heads are the sibling geometries that carry the same channels, whether written out one by one or repeated with geometry references; the beams inside a head become its parts.
- **Open Fixture Library**: matrix fixtures (one cell per pixel).
- **QLC+**: heads.
- **The fixture editor**: the **Cell** column and **Cells…**.

## Outputs and the DMX monitor {#outputs}

![The DMX outputs screen with the DMX monitor below](images/outputs.png)

**Outputs** in the rail sets where DMX goes. Outputs are settings of *this computer*, not of the show: they stay the same whatever show you open.

### The output list

Each row is one output:

| Column | Meaning |
| --- | --- |
| **On** | Untick to stop sending this output. |
| **Protocol** | Art-Net, sACN, Enttec DMX USB Pro or Enttec Open DMX. |
| **Sent as** | The universe number on the network. Console universe 1 can go out as Art-Net 8. |
| **Network interface / port** | The network card (or USB port) to send from. *Any* lets the system pick. |
| **Destination** | An IP address to send to. Empty broadcasts Art-Net and multicasts sACN. |
| **Status** | Whether it's sending. |

### New universes get an output

When a universe is patched for the first time, Lumora adds an output for it with the defaults at the top of the screen: the protocol, the network card, and whether console universe 1 is Art-Net 0 or Art-Net 1 (sACN universes match the console's). Every output is on unless you untick it.

> **Tip:** Art-Net numbers universes from 0, and different nodes count differently. If your node shows nothing, try the other setting of *Universe 1 → Art-Net 0 / Art-Net 1*.

### Sending one universe to several places

Select an output and press **Add output** to send the same universe somewhere else too, for example Art-Net and sACN at once.

### USB DMX interfaces

Plug in the interface, press **Look again**, then **Add output → USB DMX interface** (or switch an output's protocol to Enttec and pick its port). The line above the list shows which interfaces are plugged in.

### More settings

**More settings…** has the sACN priority.

### The DMX monitor

The bottom half shows the 512 channels of a universe: what Lumora is sending right now. Channels above zero are lit. It follows the output you select, or pick a universe in its box. A **DMX monitor** window can also be added to any view (**+ → DMX monitor**).

## MIDI {#midi}

![The MIDI screen](images/midi.png)

Any MIDI controller (a fader box, a keyboard, a pad controller) can drive Lumora's executors, the grand master and more.

### MIDI learn

1. Plug in your controller. Lumora listens to every MIDI input by default.
2. Turn on **Learn MIDI** in the top bar. Controls you can map get a dashed magenta outline. Learn MIDI works on the executor windows already on screen (open a Playback view, or add an Executors window), and nothing pops up over the encoders.
3. Click a fader, button or knob on any executor. It turns solid magenta while it listens.
4. Move a fader, turn a knob or press a key on your controller. It's mapped.
5. Repeat for other controls, then turn Learn MIDI off.

You can also right-click an executor and choose **MIDI learn: fader** (or a button), or on the MIDI screen pick an action (such as **Grand Master**) and press **Learn**.

The **encoder bar** maps too: with Learn MIDI on, its knobs get the same outline and their number (E1, E2…). Click one and turn a knob or endless encoder on your controller. See [Encoders and MIDI](#encoders-midi).

Notes, control changes (CC), pitch bend (14-bit faders) and program changes all work. A control can only do one thing; learning it again moves it.

### Pages

Mappings follow the current executor page, so the same physical faders drive executors 201–210 on whichever page is showing.

### Pickup and feedback

**Pickup** (on by default for faders) means a physical fader only takes over once it passes the on-screen fader's level, so nothing jumps when you change pages. If your controller has motor faders or LEDs, choose it as the **Feedback output**: Lumora sends fader positions and button lights back, and fader mappings skip pickup.

### The MIDI screen

- **MIDI devices**: the inputs found, **Listen to every input**, the feedback output and **Rescan devices**.
- **Mappings**: every mapping with its control, action, pickup, invert and endless. **Remove mapping** deletes one. **Invert** flips a fader's (or encoder's) direction. **Endless** marks an encoder mapping as an endless encoder rather than a knob.
- **Incoming**: the last MIDI messages received, to check your controller is seen.

Mappings are saved with the show.

# Programming

## Selecting fixtures {#selecting}

Almost everything starts with a **selection**: the fixtures you're about to change.

### Ways to select

- **The command line**: `Fixture 1 Thru 8`, `1 + 3 + 5`, `Fixture 1 Thru 10 - 5` (a bare number means Fixture).
- **Groups**: tap a group tile, or type `Group 2`.
- **The fixture sheet**: click rows. Click a selected row again to take it out.
- **Fixture tiles**: the tile layout of the fixture sheet; Shift-click adds a range. A fixture with subfixtures has a small arrow in its bottom-right corner: click it to open the fixture, and its cells or heads follow it on a shared tray.

### Selections add up

Like most consoles, selections add up until you clear them. `1` then `3` then `Group 2` selects all of them. **Ctrl**-tap a group to take its fixtures back out. **Clear** (or **Esc**) empties the selection.

### Selection order

The order you select fixtures in matters: it's the order effects and spreads lay out across. `Fixture 4 Thru 1` selects them backwards.

### Next and Back: one fixture at a time

**Next ›** (on the command keys, **Page Down**, or `Next`) narrows the selection to its first fixture; pressing it again moves to the second, and so on. **‹ Back** (the key next to it, **Page Up**, or `Prev`) goes the other way. After the last fixture (or before the first) the whole selection comes back. While you step, values, presets and encoders only touch that one fixture, so you can trim a single light inside a group. `All` returns to the whole selection straight away. The command line shows where you are, such as *Fixture 3 (3 of 8)*.

### Select last

**Ctrl+L** (or `Last`) brings back the selection you had before the last **Clear**. Press it again to swap back to the one you replaced, which is handy for going between two sets of fixtures.

### Highlight

**Highlight** (top bar, or **H**) makes the selected fixtures bright and open so you can find them on stage, using each channel's highlight value (see [The fixture editor](#fixture-editor)). Turn it off when you're done; it doesn't change your values. Highlight follows **Next** and **Back**, so you can step through a group and see each light on its own.

## The programmer {#programmer}

The **programmer** holds the values you're working on. Whatever is in it goes to the stage, on top of everything else that's playing.

### Putting values in the programmer

- **At**: `At 50` sets intensity to 50%. `At Full` and `At Zero`; `At At` (pressing At twice) is full.
- **Encoders**: drag, scroll or type a value (see [Encoders](#encoders)).
- **Presets**: tap a preset tile, or type `Color.3` or `At Color.3`.
- **Any attribute by name**: `Attribute Zoom At 30`. Pan and tilt take degrees: `Attribute Pan At -90`.
- **The color picker**: see [Color](#color).

Values in the programmer show in **coral** in the fixture sheet and on the encoders.

### Clearing

**Clear** works in steps, like on most consoles:

1. If fixtures are selected, Clear deselects them (their values stay in the programmer).
2. Pressed again with nothing selected, Clear empties the programmer.

**Clear All** (`ClearAll`, **Shift+Esc**) does both at once.

### Taking values out

- `Remove` drops the selected fixtures' values from the programmer.
- `Remove Fixture 3 Thru 5 Cue 2` drops fixtures from a stored cue.

### Defaults {#defaults}

Every fixture has a default for each attribute: where it rests when nothing plays.

- **Default Default** puts the selection's default values into the programmer: press the **Default** key twice, press **D** twice, or type `Default`. With nothing selected, or `Default All`, it does every fixture.
- The encoder bar's **Default** button does the current feature for the selection; press it again within a second and a half for every attribute.
- `Store Default` stores the programmer's values as the new defaults for those fixtures (presets stay references). `Store Default /overwrite` starts the defaults over; `Delete Default` removes stored defaults for the selection, or all of them.

`Home` works as another name for `Default`.

### Update

`Update` merges the programmer into the cue that's live on the selected sequence. `Update Cue 3` and `Update Color.1` merge into a specific cue or preset.

## Park {#park}

**Parking** freezes attributes where they are on stage. A parked attribute holds its output whatever sequences play, whatever is in the programmer, and through Highlight, until you unpark it. Use it for a work light that must stay on, a mover that has to stay put during a focus, or a fixture that's misbehaving.

### Parking

| You type | Parks |
| --- | --- |
| `Park` | Everything on the selected fixtures. |
| `Fixture 101 Park` or `Park Fixture 101` | Everything on fixture 101 and its subfixtures. |
| `Fixture 101 Intensity Park` | Only its intensity. Features work the same way: `Position`, `Gobo`, `Color`, `Beam`, `Focus`, `Control`. |
| `Fixture 101 Attribute Pan Park` | Only its pan. |
| `Attribute Pan Park` | Pan on the selected fixtures. Click **Pan** on the encoder bar to type the `Attribute Pan` part. |
| `Park Group 2 Position` | Position on every fixture in group 2. |

An attribute parks at the value it has on stage at that moment. Parked attributes are saved with the show.

### Unparking

`Unpark` works the same way (`Fixture 101 Unpark`, `Unpark Fixture 101 Attribute Pan`, `Unpark Group 2`). `Unpark All` releases everything.

### The Park window

![The Park window](images/park.png)

**+ → Park** lists every parked fixture; open one to see its attributes and the values they're held at. Select rows and press **Unpark** (or double-click a row), or **Unpark all**.

## Encoders {#encoders}

![The encoder bar with the Dimmer category](images/encoders-dimmer.png)

The encoder bar shows a knob for each attribute the selected fixtures have, one feature at a time.

### Using the encoders

- **Pick a feature** on the left: Dimmer, Position, Gobo, Color, Beam, Focus or Control. Features the selection doesn't have are greyed out.
- **Drag** a knob up or down, or **scroll** over it.
- **Double-click** a knob to type a value.
- **Right-click** a knob for its value, its named ranges (gobos with their pictures, color wheel slots, shutter modes), **Default**, **Full** and **Zero**.
- **Fine**, or holding **Shift** on the keyboard, makes small steps: dragging, scrolling and turning a MIDI encoder all go a tenth as far.
- **‹ ›** page through more knobs. A page has up to five (change it in [Interface settings](#interface)), fewer when they don't fit.
- **Default** puts this feature's defaults in the programmer (twice: every attribute).
- **Click a knob's name** to put `Attribute Pan` (or whichever it is) on the command line, ready for `Park`, `At 30` or `Unpark`.

Values show, and are typed, in the channel's own units: percent for most, degrees for pan and tilt, or the value range the fixture type gives a channel. On a zoom of 5 to 50, `Attribute Zoom At 30` (or typing 30 after a double-click) sets it to 30.

The ring around each knob shows where the value sits in its range. Coral means the value is in the programmer. The knobs follow the output live, so a cue fading in moves them too.

The label above the categories says how many fixtures are selected, and the spread on them if there is one (see [Spreads](#spreads)).

The arrow in the bar's top-right corner hides it, leaving a thin strip with the arrow to bring it back.

### Encoder windows

**+ → Encoders** adds the encoders as a window, anywhere in a view and on any screen. Each one has its own feature and page, so one window can stay on Position while the bar shows Color. Right-click its tab to give it its own number of encoders per page. Windows don't answer MIDI; the bar does.

### Encoders and MIDI {#encoders-midi}

A MIDI knob or endless encoder maps to an encoder by its **place on the bar**, not by its attribute: map your first knob to the first encoder and it turns Pan on the Position page, Dimmer on the Dimmer page, and whatever comes first on the next page. Changing feature or page takes your controller with it.

- **Learn it**: turn on **Learn MIDI**, click an encoder on the bar (it shows E1, E2…) and turn the control. Or right-click the encoder → **MIDI learn: encoder**, or pick **Encoder 1** on the MIDI screen.
- **Endless encoders and knobs** both work. An endless encoder (sending 65/63, or 1/127, for each step) moves the value a step at a time; a knob moves it as far as you turn it, from wherever it is, so nothing jumps when the page changes. Lumora guesses which you have from the first message; tick or untick **Endless** on the MIDI screen if it guessed wrong.
- **Fine**: press Fine on the bar, or hold Shift on the keyboard while turning.
- **The push**: right-click an encoder → **MIDI learn: push**, then press a button (or an encoder's push) on your controller. Pressing it opens that encoder's ranges, the same as right-clicking it. The push is only learned this way, not by clicking with Learn MIDI on.
- **Forget MIDI mappings** in the same right-click menu removes both.

### Framing shutters {#shapers}

![The Beam category with two profiles selected: the shaper view, its buttons and the blade knobs](images/shaper.png)

When the selection has framing shutters, the **Beam** category shows them drawn over the beam, like a console's shaper view: the light grey circle is the beam, the dark grey blades cover it, and a white line marks each blade's edge. Blade 1 is at the top, 2 on the right, 3 at the bottom, 4 on the left; the whole picture turns with the module's rotation (**Blade rotation**).

- **Drag a blade's number** in or out to set its insertion. Press anywhere and the nearest handle is picked up.
- **Drag its A or B end** to swivel the blade (**Ins + Rot**). On fixtures whose blades have two corners (A and B parameters), switch to **A + B** and the ends move each corner on its own, for triangles and slants.
- **Link** moves other blades with the one you drag: **all** of them, the opposite blade the same way (**parallel**, which slides the opening across), or the opposite blade the other way (**mirrored**, which closes in from both sides).
- **Reset blades** takes the blades out; **Reset shaper** also straightens the swivels and the module.

The knobs beside it are the same blades as numbers. The demo show has two profiles with framing shutters (group 5, Profiles).

## Position: pan, tilt, the XY pad and Flip {#degrees}

![The XY pad and Flip in the Position category](images/encoders-position.png)

### Pan and tilt in degrees

Lumora programs pan and tilt in the fixture's physical degrees, with **0 in the middle** of its range. A 540° pan runs from -270 to 270; a 270° tilt from -135 to 135. Every fixture rests at 0°, 0°.

Presets, cues, defaults and effects all keep degrees. A Universal position preset therefore points fixtures with different ranges the same way.

The ranges come from the fixture file (GDTF physical ranges, Open Fixture Library angles, QLC+ pan and tilt maximums). When a file doesn't say, Lumora assumes 540° pan and 270° tilt; you can set the real range in the fixture editor's **Degrees** column.

On the command line, `Attribute Pan At -90` and `Attribute Tilt At 45` take degrees. `Full` and `Zero` mean the ends of the range.

### The XY pad

In the Position category, the **XY pad** shows every selected fixture as a dot: pan across, tilt up and down, each across its own range. The first selected fixture is ringed; its pan and tilt are written underneath. Drag on the pad to move all the selected fixtures together. Hold **Shift** (or turn on **Fine**) for small moves.

### Flip

A moving head can point at the same spot two ways: pan one way with tilt forward, or pan half a turn round with tilt mirrored. **Flip** (next to the pad, or type `Flip`) switches the selection to the other way. Pan goes half a turn round, choosing whichever way stays nearer the middle of its range, and tilt mirrors. A fixture without room to go round is left alone. Flip is useful when a fixture would otherwise swing a long way between cues.

## Color {#color}

![The color picker](images/color-picker.png)

There are three ways to set color:

1. **Color presets**: tap one (the demo show has the thirteen standard colors and a 3200K warm white).
2. **The Color encoders**: one knob per emitter or wheel.
3. **The color picker** window (**+ → Color picker**).

### The color picker

Four tabs:

- **Gels**: Rosco and Lee gels by number or name. Type to filter.
- **Standard**: named colors plus a hue/saturation field.
- **Temperature**: 1800–10000 K. Fixtures with warm and cool white blend them.
- **Gamut**: click inside the LED gamut on a CIE 1931 diagram.

Whatever you pick goes to the selected fixtures using what they have: RGB (with white, amber, lime or UV) mixing, CMY, or the nearest named slot of a color wheel.

### Color presets from the picker

Pick a gel, a standard color or a color temperature, then store a Color preset (**Store**, then tap an empty Color tile, or `Store Color.5`). The preset is named after what you picked:

| You picked | The preset is called |
| --- | --- |
| A gel | Its number and name: "L201 Full CT Blue" |
| A standard color | Its name: "Red" |
| A temperature button | Its name and temperature: "Tungsten 3200 K" |
| A point on the temperature bar | The temperature: "4150 K" |

Rename it like any preset (right-click → **Name**). A name you give when storing (`Store Color.5 "Front blue"`) always wins, and so does the name of a preset you merge into. Once you change the colors in the programmer by other means (encoders, another preset, the hue field or the gamut), the next preset gets an ordinary name again.

## Groups {#groups}

![Groups and color presets pools](images/pools.png)

A **group** is a stored selection, in order.

- **Store a group**: select fixtures, press **Store**, tap an empty group slot. Or type `Store Group 3 "Front"`.
- **Use it**: tap the tile (adds its fixtures to the selection), or type `Group 3`. **Ctrl**-tap takes its fixtures out.
- **Edit it**: right-click the tile: **Name**, **Update with selection** (replaces its fixtures with the current selection), **Delete**.
- A group tile is outlined when the selection is exactly that group.

Groups are referenced by blueprint lines, so reordering a group (storing it again in a different order) changes the cues that use it.

## Presets {#presets}

A **preset** is a named set of values for one feature: a color, a position, a gobo, a zoom. Cues store references to presets, so editing a preset changes every cue that uses it.

### Preset pools

There are eight pools: **Dimmer**, **Position**, **Gobo**, **Color**, **Beam**, **Focus**, **Control** and **All** (which stores every attribute). The presets window shows one pool at a time; switch with the tabs along its top. On the command line presets are written by pool name and number: `Dimmer.1`, `Position.2`, `Gobo.3`, `Color.4`, `Beam.5`, `Focus.6`, `Control.7`, `All.8` (or `Preset 4.1`).

### Storing a preset {#storing-presets}

1. Select fixtures and set the values (for example a color).
2. Press **Store** and tap an empty slot in the right pool, or type `Store Color.5 "Deep blue"`.

Only the programmer values that belong to the pool's feature are stored. Storing into an existing preset merges; add `/overwrite` to replace it.

### Layers: Universal, Global, Selective

A preset can hold three layers. For each fixture, the most specific layer that has a value wins:

| Layer | Holds | Wins over |
| --- | --- | --- |
| **Universal** (U) | One value per attribute, for any fixture that has it. | — |
| **Global** (G) | One value per fixture type. | Universal |
| **Selective** (S) | One value per fixture. | Global and Universal |

Dimmer and Color presets store **Universal** by default, so "Red" works on every fixture with red. Position, Gobo, Beam, Focus, Control and All store **Selective**, because a position belongs to particular fixtures. Add `/universal`, `/global` or `/selective` to store another layer (`Store Color.3 /selective` gives a few fixtures their own version of a universal color), or right-click a preset and choose which layer to update. Each tile shows the layers it has.

### Applying presets

Tap a preset tile with fixtures selected, or type `Color.3`. The programmer stores a *reference* to the preset, and the fixture sheet shows the preset's name instead of numbers.

### Editing presets

Right-click a tile: **Name**, update it with the programmer (choosing the layer), or **Delete**. To change what a preset holds, apply it, adjust the values, and store or update it again.

### Effects in presets

Storing a preset also stores any running effect that drives that preset's feature (an **All** preset takes every running effect), **with its spread**: a strobe laid out with a random spread plays random wherever the preset goes. The preset's layer says where the effect plays, just like its values:

| Stored as | The effect plays on |
| --- | --- |
| **Universal** (`/universal`) | Whatever the preset is applied to. |
| **Global** (`/global`) | Fixtures of the same types as the ones it was stored from. |
| **Selective** (`/selective`) | Only the fixtures it was stored from. |

For example: select some pars, `Spread Direction Random`, run the Strobe effect, then `Store All.5 /universal`. Now `All.5` on any selection strobes it with random offsets. Or run a Circle on one spot and `Store Position.8 /global`: applying Position.8 circles every spot of that type, and leaves other fixtures alone.

Applying the preset runs its effects on the selection (as far as each layer reaches), with the effect's stored spread, or the active spread if it has none.

## Spreads {#spreads}

![The Spreads pool](images/spreads.png)

A **spread** changes how the selection is used. It can thin the selection (only every other fixture, say) and it decides how effects roll across the fixtures.

### What a spread holds

| Setting | Effect |
| --- | --- |
| **Every Nth fixture** and **Starting at** | Interleave: which of the selected fixtures take values and effects. Every 2 starting at 1 is fixtures 1, 3, 5…; starting at 2 is 2, 4, 6… |
| **Phase from / to** | How an effect's phase spreads across the fixtures, from -720 to 720 degrees. 0 to 0 moves everything together; 0 to 360 is a full chase; -360 to 0 runs it the other way; 0 to 720 goes round twice. |
| **Blocks** | Neighbors share a phase: Blocks 2 moves pairs together. |
| **Groups** | The phase repeats every N fixtures. |
| **Wings** | The layout is mirrored: Wings 2 runs from both ends to the middle (or out). |
| **Direction** | Forward, backward, ends to middle, middle to ends, or random. |

### The active spread

The spread on the selection is the **active spread**. It goes with the selection: clearing the selection clears it. The encoder bar shows it next to the selection count, and the Spreads window's header shows it.

While a spread with an interleave is active, values you set (`At 50`, presets, encoders) go only to the fixtures it picks.

### The Spreads window

Add it with **+ → Spreads**.

- **Tap a tile** to put that spread on the selection; tap it again to take it off.
- **The header** shows the active spread; tap it to change any setting, or press **Off**.
- **Store** then tap a slot keeps the active spread in the pool.
- **Right-click** a tile to rename it, change its settings, put it on the selection, update it with the active spread, or delete it.

The demo show starts with Odd, Even, Together, Wings 2, Blocks 2 and Middle out, plus the spreads its effects use (see [How effects spread across fixtures](#effect-spreads)).

### On the command line

```
Spread 3                                  put spread 3 on the selection
Spread Off                                take it off
Spread Every 2 Offset 2                   every other fixture, starting with the second
Spread Wings 2 Blocks 2                   change the active spread
Spread Phase -360 Thru 0                  phase range in degrees
Spread Direction Backward                 forward, backward, inward, outward, random
Store Spread 3 "Evens"                     keep it in the pool
Blueprint Cue 1 Group 2 Effect 1 Spread 3 use it on a blueprint line
```

## Effects {#effects}

![The Effects pool and the effect editor](images/effects.png)

An **effect** makes values move by themselves: a dimmer chase, a color rainbow, a circle of movement.

### Layers

An effect is a stack of **layers**, each driving one part of a fixture:

- **Dimmer**: intensity.
- **Position**: pan and tilt.
- **Color**: every color system a fixture has: red, green and blue, white (or warm and cool white), amber, orange, yellow, lime, mint, cyan, indigo, purple and magenta emitters (each takes the part of the color it can make, the rest goes to RGB), hue and saturation on virtual-color fixtures, CMY flags, CTO and the color wheel. Switch off any family per layer to leave it alone; its share goes to the others.
- **Attributes**: any other attributes you pick (zoom, iris, gobo rotation…).

**Relative** (Dimmer and Attributes layers): the points are amounts added to whatever value is already there, from a running executor or the programmer, instead of the values themselves. They go from -100 to 100, so an effect can take a level down as well as up: -50 to 0 dips an 80 % executor between 30 % and 80 %, and never lifts a fixture that's out. With Relative on, a plot under the points draws the wave from low to high around zero, and the preview runs over the live output. The library's **Dip** is one.

![A relative Dimmer layer (the library's Dip): low -50, added to the level underneath](images/effect-relative.png)

### Modes

| Mode | What it does |
| --- | --- |
| **Wave** | Between a low and a high point, following a *shape*: sine, ramps, triangle, square, pulse, breathe, random, smooth random, or one you draw. |
| **Steps** | Through any number of points, with a crossfade (0 snaps, 100 fades the whole step), in order, there and back, or at random. |
| **Form** | Position only: a circle, figure 8, pan or tilt line, square or triangle, around where each fixture points or around a position preset. Sizes are shares of each fixture's range. |

**Points** can be levels, colors, pan/tilt values in degrees, or references to presets, so an effect holds no fixture-specific data and runs on whatever you select.

**Shapes** are shared: draw one with **Draw new…**, and editing it later updates every effect that uses it.

Each layer has a **speed** multiplier and a **phase offset**, so a color can roll at half the speed of a dimmer chase.

### How effects spread across fixtures {#effect-spreads}

How an effect rolls across the fixtures (its phase, blocks, groups, wings and direction) comes from a [spread](#spreads) in the pool. Each effect has one, chosen under **Spread** in the effect editor; effects that share a spread change together when you edit it. Effects made before this, and ones from the library, get the pool spread that matches their layout (made for them if there's none, named like "Chase 360°" or "Together"). **None** keeps the effect's own layout.

A spread on the selection when you run the effect, or on the blueprint line it's in, lays it out instead.

### The Effects pool

- **With fixtures selected, tap** an effect to run it on them. Running effects are listed under the pool with live **Rate** and **Size** sliders, the spread they run with, and **Stop**.
- **With nothing selected, tap** an effect to open it in the editor.
- **Right-click** a tile: name, **Edit effect…**, **Run on selection**, **Stop**, **Duplicate**, **Delete**. Right-click an empty slot to create a blank effect or one from the **library** (19 ready-made effects: dimmer waves and chases, circles, fans, a smooth rainbow, color swaps, a strobe, a random strobe, a relative dip, and more).

In the demo show the **Effects** view has the ten [rate masters](#rate-masters) under the pool, to set the effects' speed.

### The effect editor

Add it with **+ → Effect editor**. It shows the effect's **name** and **seconds per cycle** (with the BPM it makes), its **layers** (add with **+ Dimmer**, **+ Position**, **+ Color**, **+ Attributes**), the selected layer's settings, and a live **preview**: one row per selected fixture across one cycle, with a playhead. **Run on selection** starts it.

### On the command line

```
Effect 12                  run effect 12 on the selection
Effect 12 Rate 150 Size 50 adjust it (percent, percent)
Effect 12 Off              stop it
Effect Off                 stop the effects on the selection
```

### Effects in cues

Storing a cue stores the running effects. Blueprint lines can carry effects on a group (`Blueprint Cue 4 Group 1 Effect 12`), and a preset in a blueprint line brings its stored effects too: `Blueprint Cue 4 Group 1 All.5` puts All.5's strobe, with its spread, on group 1 (a spread on the line replaces the preset's). An executor's fader scales an effect's intensity like any other value.

**Effects follow the cue's fade.** Going to the next cue, the old cue's effects fade out and the new cue's fade in over the cue's fade time (after its delay), and an Off fade fades them out too. A cue with no fade still changes at once. Effects run in the cue they're stored in; they don't track into later cues yet.

# Cues and playback

## Storing {#storing}

![Store armed: pool slots and executors light up red](images/store.png)

**Store** is how anything gets kept. Press **Store** (or **S**), then tap where it goes:

| Tap | Stores |
| --- | --- |
| A group slot | The selection as a group. |
| A preset slot | The programmer's values for that pool's feature. |
| A sequence slot (in a sequence pool) | A cue into that sequence. |
| An executor | The next cue of its sequence. An empty executor gets a new sequence. |
| A view in the rail | The windows on screen into that view. |
| A spread slot | The active spread. |

When the view has no executor window, Store shows a tile for every executor in the 100, 200, 300 and 400 rows where the encoders usually are. **Esc** cancels Store.

You can also type: `Store Cue 5`, `Store Group 3 "Front"`, `Store Color.1 "Red" /overwrite`, `Store Sequence 2 Cue 1.5`, `Store Cue 1 Thru 40` (a whole run of empty cues).

## Sequences and cues {#sequences}

![The cue list of the demo sequence "Pars"](images/cue-list.png)

A **sequence** is a list of **cues**. The **Sequences** view has the **sequence pool** on the left and the **Sequence editor** on the right. Like the effects pool and the effect editor, tap a sequence in the pool and the editor shows its cues.

### The sequence pool

**+ → Sequences** adds a sequence pool window. Tap a sequence to select it: every Sequence editor shows it, and it becomes the sequence `Cue` commands work on. Right-click it for:

- **Name**.
- **Priority**: Low, LTP, HTP or Ultra (see [Priority](#priority)).
- **Off when overridden**: Yes or No.
- **Appearance…**: the sequence's color, from the standard colors or the gamut (or **No color**). It shows as a chip on its pool tile, a stripe along the top of its executors and a tint in their faders.
- **Assign to executor…** and **Delete sequence**.

Tiles show the priority in the corner when it isn't LTP.

### The Sequence editor

**+ → Sequence editor** adds one. It edits the selected sequence (tap one in a sequence pool, or type `Sequence 2`), and its tab names it: "Sequence editor · Pars".

| Column | Meaning |
| --- | --- |
| **Cue** | The cue number. Cues can have decimals (1.5) to fit between others. Double-click to renumber. |
| **Name** | A name for the cue. |
| **Fade** | Seconds the cue takes to fade in. |
| **Delay** | Seconds to wait before the fade starts. |
| **Trigger** | How the cue starts: **Go**, **Wait** or **Follow** (see below). Click or right-click it to choose. |
| **Wait** | Seconds, for a Wait trigger. Typing one makes the cue a Wait. |
| **Notes** | Notes for the operator ("Wait for the actor to sit"). |
| **Contents** | What the cue holds: blueprint lines, fixtures programmed, effects. |

Double-click or right-click a cell to edit it. Select several cues (Ctrl-click or Shift-click) to change them together: the **Trigger** dropdown, and a **Fade**, **Delay** or **Wait** typed into one of them, go to all of them. The live cue is shown in amber. A cue with an orange dot uses something that was deleted; hover over it for what's missing.

**Buttons** under the list:

- **Top**: back to the first cue (and takes the playback there, if the sequence is running).
- **New cue**: a blank cue at the end, ready for blueprint lines.
- **Store new cue**: stores the programmer as a new cue at the end.
- **Update cue**: merges the programmer into the selected cue.
- **Go to cue**: plays the selected cue now.
- **Delete cue**.

**Tracking** (on by default), **Wrap to first cue** (Go after the last cue goes back to the first), **Off when overridden** and the **priority** are at the top right.

### Priority {#priority}

| Priority | How it plays |
| --- | --- |
| **Low** | Below everything. Its values only show where nothing else has any, and they never turn other sequences off. Its effects still run over values from higher priorities (see [Effects and priority](#effect-priority)). |
| **LTP** | The default. Latest takes precedence for every attribute, intensity included: the sequence that went most recently wins. |
| **HTP** | Above LTP. Among HTP sequences, the highest intensity wins; other attributes are latest takes precedence. |
| **Ultra** | Above everything, the programmer included. |

A higher priority always wins, whichever sequence went last. Within a priority, a sequence's Go (or turning it on) makes it the latest.

Each executor's fader scales its sequence's intensity before they're combined, so with LTP a sequence at half on its fader shows half its intensity, even over a brighter one that went earlier.

### Effects and priority {#effect-priority}

Lumora keeps values (absolute) and effects (relative) apart. First the values of every running sequence and the programmer are combined by priority, as above. Then the effects run on top of the result: a circle goes round whatever position won, a dimmer wave moves around whatever level won.

Effects only compete with other effects. For each fixture and attribute the effect with the higher priority wins, then the most recent; the programmer's effects sit above every sequence's but Ultra.

| Running | What you see |
| --- | --- |
| A circle (LTP), then a position on another executor (LTP) | Both stay on. The circle goes round the new position. |
| A Low color effect, then an LTP color preset | Both stay on. The color effect keeps running. |
| Then a second Low color effect | It takes over from the first, the newest Low effect winning. The first stays on underneath. |
| A position and a circle on one executor, then a position on another | The first executor is overridden and turns off. |

An effect fades with its cue's fade: in with the cue, out as the next cue comes in or the executor goes off.

**Stomp.** Calling a preset onto fixtures that are running an effect in the programmer stops that effect on those fixtures, for the features the preset has values for. The preset's own effects then start.

### Off when overridden

On by default. When other sequences have taken over every value a sequence has (each fixture and attribute it controls now comes from another sequence), it turns itself off, at once. So ten sequences holding only color replace each other: turning one on turns the previous one off. A sequence with values is judged by its values, and its effects go with it. A sequence with only effects is judged by its effects, and only other sequences' effects take those, never values: a position doesn't turn a circle off. A sequence held on by a Flash, Temp or Swop key stays on, values and effects from **Low** sequences never turn anything off, two **HTP** sequences never take intensity from each other, and the programmer never turns a sequence off. Turn the option off to keep a sequence running underneath.

### Cue triggers {#cue-triggers}

Each cue has a **trigger**: how it starts.

| Trigger | The cue starts |
| --- | --- |
| **Go** | When you press Go. The default. |
| **Wait** | Its wait time after the cue before it started, by itself. `Wait 5`: five seconds after the previous cue's Go. |
| **Follow** | By itself, as soon as the cue before it has finished (its delay and fade). |

The trigger belongs to the cue it starts: to have cue 3 run straight after cue 2, set cue **3** to Follow. Click (or right-click) the **Trigger** cell and choose Go, Wait… (it asks for the seconds) or Follow; with several cues selected, they all change. On the command line: `Cue 3 Follow`, `Cue 3 Wait 5`, `Cue 3 Go`. A run of Follow and Wait cues plays by itself; with **Wrap to first cue** on and the first cue set to Follow or Wait, the sequence loops. Pausing the playback holds the clock too.

### Individual fade and delay times {#individual-times}

A cue's **Fade** and **Delay** are for everything in it. Values can also have **individual** times, so fixtures fade one after another. Times are given as a range, and the range is spread across the fixtures **in selection order**: select 1 Thru 8 and fixture 1 gets the first time, 8 the last; select 8 Thru 1 and it runs the other way. With a spread on the selection, its blocks, groups, wings and direction lay the times out the same way they lay out effects.

- **In the programmer**: `Fixture 1 Thru 8 At 100 Fade 0 Thru 4` sets the level with fades from 0 to 4 s. On its own, `Fade 0 Thru 4` (or `Delay 0 Thru 2`, or `Time 0 Thru 4`) times the selection's programmer values. `At Fade 0 Thru 4` works too. Three numbers make a V: `Fade 0 Thru 3 Thru 0` is fastest at both ends. A feature or attribute after it limits it: `Fade 0 Thru 4 Color`, `Attribute Pan At Fade 0 Thru 2`. `Fade Off` and `Delay Off` take the times away. Setting a new value keeps a value's times.
- **Stored in the cue**: storing keeps the individual times with the values. There are no separate columns for them: the cue list's **Fade** and **Delay** show one time when everything in the cue uses it, and the range the cue's times span (`0 thru 3`) when its values have individual times. The cell's tooltip still gives the cue's own fade and delay.
- **On a stored cue**: type a range in the cue's **Fade** or **Delay** cell (`0 thru 5`), or `Cue 3 Fade 0 Thru 5`. It spreads across the selected fixtures in the cue, in selection order, or across all of the cue's fixtures when nothing is selected. Typing one number gives the cue that time again, taking its values' individual times away (`Cue 3 Fade Off` takes them away and keeps the cue's own). Blueprint lines keep their own times: they override the cue's. With several cues selected, they all take it.
- **On a blueprint line**: the blueprint's **Fade** and **Delay** columns time the line's fixtures, in the group's order (laid out by the line's spread, with its blocks, wings and direction, when it has one), and the line's **effects** take the same times: they fade in fixture by fixture with their values. A line's times override the cue's Fade and Delay, and stay when you type a new time for the cue. Empty is the cue's own. On the command line: `Blueprint Cue 2 Group 1 At 100 Fade 0 Thru 3 Delay 1`. **Build into values** keeps the times on the built values.
- **Typing a range**: in any of these cells, pressing **T** types *thru*: `0 T 5` reads `0 thru 5`.

Values without individual times use the cue's. The cue counts as finished when the longest delay plus fade is done, which is when a **Follow** cue after it starts. A crossfade fader stretches the whole thing, individual times included, over its travel. **Go Back** and **Goto** without a fade ignore individual times.

### The Off line

Every sequence ends with a greyed-out **Off** line. Its **Fade** and **Delay** are the times the playback fades out with when the executor is turned off (an Off button or `Off Executor 201`). Intensity fades down and everything else holds until the fade ends. Letting go of a flash or pulling a fader to zero still stops at once.

The Off line has a **Trigger** too. Set it to **Follow** and the sequence turns itself off once the last cue has finished; **Wait** turns it off that many seconds after the last cue started. This comes before **Wrap to first cue**. Left on **Go** with wrapping off, **Go** on the last cue turns the sequence off, and the next Go starts it from the top. On the command line: `Cue Off Follow`, `Cue Off Wait 5`, `Cue Off Go`, `Cue Off Fade 3`.

### Cue commands

```
Store Cue 5                       store into the selected sequence
Store Sequence 2 Cue 1.5          store into a specific sequence
Store Cue 1 Thru 40               lay out a run of empty cues
Cue 3 Fade 4                      set the fade (also Delay)
Cue 3 Note "Wait for the actor"   set its notes
Cue 3 Follow / Cue 3 Wait 5 / Cue 3 Go   its trigger
Cue Off Follow / Cue Off Wait 5   the sequence turns itself off after the last cue
Label Cue 3 "Sunrise"             name it
Update Cue 3                      merge the programmer into it
Delete Cue 3
Copy Cue 2 At 5 / Move Cue 2 At 5
```

## Blueprints {#blueprints}

![A cue's blueprint](images/blueprint.png)

A cue can hold values directly (what you stored from the programmer), or be described by a **blueprint**: a list of lines, each saying "take this group, apply these presets (and effects)". Blueprints are worked out live when the cue plays, so changing a group or a preset changes the cue without storing it again. A cue can have both: its own values override the blueprint.

### Blueprint lines

Select a cue and use the **Blueprint** table under the cue list:

| Column | Meaning |
| --- | --- |
| **Name** | A name for the line. |
| **Group** | The group it applies to. |
| **Presets** | Click to choose presets: a tab per pool plus **All presets**. Intensity comes from a Dimmer preset (the demo has Full, 25% and Out). |
| **Every**, **Offset** | Take every Nth fixture of the group, starting from this one. |
| **Spread** | A spread from the pool: which of the group's fixtures, and how the line's effects spread across them. |
| **Fixtures** | How many fixtures the line reaches. |

**Add line** adds a line (on the first group, at Full when there's a Full preset); **Remove line** removes the selected one. A line whose group, preset, effect or spread was deleted gets a red dot. It keeps pointing at the old number, so storing a new one there fixes it.

### Build into values

**Build into values** (or `Build Cue 2`) turns a cue's blueprint into ordinary cue values, keeping preset references. Do this when you want to edit fixtures individually.

### Blueprint-only cue lists

Many shows can be programmed entirely in blueprints: lay out empty cues with `Store Cue 1 Thru 40` or **New cue**, then give each a few lines.

```
Blueprint Cue 2 Group 1 Dimmer.1 Color.2
Blueprint Cue 2 Group 1 At 80 Color.2          At 80 adds an 80% universal Dimmer preset
Blueprint Cue 2 Group 1 Every 2 Offset 2 Color.3
Blueprint Cue 4 Group 1 Effect 12 Spread 3
BP Cue 5 Group 2 Position.1 "Spots home"       BP is short for Blueprint
Build Cue 2
```

## Executors and playback {#executors}

![Executors in the Playback view](images/executors.png)

Sequences play back on **executors**: on-screen faders, buttons and knobs.

### Executor rows

Executors are numbered by row:

| Numbers | Kind |
| --- | --- |
| **101–199** | Buttons. One key, Go by default. A button always plays at full. |
| **201–299** | Faders. One button (Go by default) and a fader. |
| **301–399** | Encoders. A knob for the master and a push key (Toggle). |
| **401–499** | Any of the three: right-click → **Executor type**, or `Executor 401 Button` (or `Encoder`, `Fader`). |

The Playback view stacks encoders 301–310 over faders 201–210, with buttons 101–110 underneath.

### Assigning a sequence

- Store onto an empty executor: it gets a new sequence.
- Right-click an executor → **Assign sequence**.
- `Assign Sequence 2 At Executor 202`, or press **Shift+A** (or type `Assign`), tap a sequence, then tap an executor: anywhere on it, not just its title.
- Right-click a sequence in the pool → **Assign to executor…**

**Unassign** (right-click) removes it. Right-click → **Appearance…** sets the sequence's color, shown along the top of the executor. The sequence's number shows small and grey beside its name on the executor, like the executor's own number.

### Rate masters {#rate-masters}

Ten **rate masters** set the speed of the effects that follow them. Each works in one of three modes:

| Mode | What the value means |
| --- | --- |
| **Rate** | A multiplier on each effect's own speed. The fader's middle is 100 %, the top 200 %; type up to 1600 %. |
| **BPM** | A tempo: every effect following it runs one cycle per beat, whatever its own speed. |
| **Ratio** | A multiplier in musical steps: ×1/16, ×1/8, ×1/4, ×1/2, ×1 (the middle), ×2, ×4, ×8, ×16. |

- **Make an effect follow one**: in the effect editor, pick it under **Rate master**, or `Assign Effect 3 At RateMaster 2`. An effect following a master ignores its own speed setting in BPM mode; in Rate and Ratio modes the master multiplies it.
- **Put one on an executor**: right-click an executor → **Assign rate master**, or `Assign RateMaster 1 At Executor 210`. Its fader sets the value and its buttons are **Learn** (tap tempo: tap it in time), **Reset** (back to 100 %, 120 BPM or ×1) and **Freeze** (stops every effect following it where it is).
![The Rate masters window: Rate 1 at 150 % with an effect following it](images/rate-masters.png)

- **The Rate masters window** (+ → Rate masters) shows all ten as faders, ten across in one row, drawn like the 200 executors: the name and value at the top, a **Learn** key, and the fader (the dotted line marks 100 % or ×1). Drag the fader, tap Learn in time to set a tempo, and right-click to name it, pick its mode, type a value, reset or freeze it. **Show it as Button** turns that master into one big Learn key instead of a fader (`RateMaster 4 Button`, `RateMaster 4 Fader`).
- **On the command line**: `RateMaster 2 At 128` sets it (percent in Rate mode, BPM in BPM mode, the multiplier in Ratio mode); `RateMaster 2` taps it; `Label RateMaster 2 "Chase"` names it.

### One sequence on several executors

A sequence can sit on as many executors as you like, and they play it **together**: it's one playback. Put sequence 1 on 101, 201, 301 and 401, set 301's push to Pause and 401's key to Off (and 201's fader to X-fade), and **Go** on 101 starts it, 301 pauses the fade, 201 crossfades to the next cue by hand, and 401 turns it off. All of them show the same cue. Master faders of the same sequence move together.

### Button functions

Every executor has one button. Right-click an executor to change what it does, or type `Assign Flash At Executor 201`:

| Function | Does |
| --- | --- |
| **Go** | Starts the sequence, or goes to the next cue. |
| **GoBack** | Goes to the previous cue. |
| **Pause** | Holds a fade; press again to continue. |
| **Flash** | Full while held, back when released. |
| **Temp** | On while held, at the fader's level. Unlike Flash it uses the times: it comes in with the first cue's fade and goes out with the Off line's fade and delay. |
| **Toggle** | On, then off. |
| **On** / **Off** | On, or off (with the sequence's off time). |
| **Swop** | Like Flash, and everything else goes dark while held. |
| **Select** | Selects the sequence (for the Sequence editor and Cue commands). |
| **Top** | Back to the first cue. |

### Fader functions

| Function | Does |
| --- | --- |
| **Master** | The fader scales the sequence's intensity. Raising it from zero starts the sequence; pulling it to zero stops it. |
| **X-fade** | The fader runs the fade to the next cue by hand. |
| **Temp** | The sequence is on while the fader is up, at its level. |

`Assign XFade At Executor 201` sets a fader's function.

### Pages

Executors come in **pages**. Above the executors, the way a pool pages through its slots: **▲** goes to the previous page, the box shows the page number (type a number to jump there; a new number makes a new page), **▼** the next. The page's name sits beside it: double-click or right-click it to name the page, or `Label Page 2 "Verse"`. `Page 2`, `Page +` and `Page -` switch pages from the command line.

**+ → Executor pages** adds a pool of pages: tap one to go there (the current page is lit), right-click to name it.

Each row holds up to **60 executors** per page: 101–160, 201–260, 301–360 and 401–460. **Show → Interface settings… → Executors per row** sets how many an executor window shows by default.

### Playback commands

```
Go Executor 201                go (also GoBack, Off, Pause, Top)
Goto Cue 3 Executor 201        jump to a cue
Executor 201 At 75             set a fader
Top Executor 201               back to the first cue
Off Executor 201               off, with the sequence's off time
```

On the keyboard: **Space** is Go and **Shift+Space** is Go Back for the selected sequence; **O** is Off.

### How playbacks combine

Sequences combine by [priority](#priority): by default everything is **latest takes precedence**, intensity included (the sequence that went last wins), and sequences that lose all their values turn off. The programmer is on top of every sequence but Ultra ones. The executor's fader (as Master) scales its intensity, including effects. The **grand master** scales everything; **blackout** turns everything off.

### Executor windows

![An executor grid, from 101](images/executor-grid.png)

Add an Executors window with **+ → Executors**: one row of executors. **+ → Executor grid** shows them as a grid instead, like a pool: from 101 by default, as many as fit.

Right-click either window's tab to set, like a pool's grid, **Executors wide** and **Executors tall** (0 fits as many as there's room for), and the **Row** (101, 201, 301 or 401) or the **First executor**. A grid runs across, then down, and stops at the end of the row's 60: a grid from 101 that's 10 wide and 3 tall shows 101–130. Every executor window has the page control, and they all follow the same page.

## Plots {#plots}

![The demo show's Rig plot, with the pars, spots and pixel bars live, and groups, presets, an executor and a macro to tap](images/plot.png)

A **plot** is a sheet you lay out to work from: fixtures where they hang, pool objects you use often, and text and pictures to find your way. The **Plot** view in new shows has the **Plots** pool and a **Plot** window side by side (add either with **+ → Plots** or **+ → Plot**).

### Fixtures on a plot

Fixtures and subfixtures show their **intensity and color** live: the symbol lights in the color the fixture is making, as bright as its intensity, with a level bar along the bottom. A selected fixture has a yellow outline; a fixture with values in the programmer has a coral dot in its corner. Pixels and other subfixtures go on as small dots.

- **Tap** a fixture to add it to the selection; tap it again to take it out. The selection keeps the order you tapped in.
- **Drag a box** on an empty part of the sheet to add everything inside it, left to right and top to bottom.
- Commands waiting for a target take the tap, so `Move`, `Copy` and the like work by tapping here too.

### Pool objects on a plot

Groups, presets, sequences, executors, effects, macros, views, spreads, rate masters and other plots can sit on a plot as tiles. Tapping one does what its pool tile does: a group adds its fixtures to the selection, a preset applies, a sequence is selected, an executor goes, an effect runs (or opens in the editor when nothing is selected), a macro runs, a spread goes on the selection, a view or plot is shown. They light up while they're in use: a group that is the selection, a preset in the programmer, a running sequence or macro.

### Putting things on a plot

- **Add ▾ → Selected fixtures** puts them on in a row under what's there (ones already on it are skipped). On the command line: `Assign Fixture 1 Thru 8 At Plot 1`, `Assign Fixture 21.1 Thru 21.8 At Plot 1`, or `Store Fixture 101 Thru 108 At Plot 1` (Store and Assign do the same here). A plot that doesn't exist yet is made.
- **Add ▾ → Pool object…**, or `Assign Group 3 At Plot 1` (or `Store Group 3 At Plot 1`), `Assign Preset 4.1 At Plot 1` (or `Color.1`), `Assign Sequence 2 At Plot 1`, `Assign Executor 1.201 At Plot 1`, and likewise Macro, Effect, View, Spread, RateMaster and Plot.
- **Add ▾ → Text** writes a label; **Icon** picks one of the built-in icons (arrow, truss, stage, speaker, person, star, shapes, door, camera, fog, moving wash, moving profile and a conventional profile); **Picture or SVG file…** adds any SVG, PNG or JPEG; **Background…** puts a picture (an SVG ground plan, say) behind everything. Pictures are kept in the show file, so the show still has them on another computer.

### Editing

Press **Edit** to arrange the plot. Drag things to move them (they snap to a grid), drag a box to pick several, Shift- or Ctrl-click to add to the pick, and drag the small square on a single picked item's corner to resize it. **Delete** removes the picked items. Right-click (or double-click) an item for its settings: a label, a fixture's shape (square or circle), a text's size and color, a picture's tint (for one-color icons) or whether it's the background, and **Remove from the plot**. Press **Edit** again to go back to using it.

The truss icon stretches: drag it longer and it adds sections rather than stretching the ones it has, and make it taller than it is wide to stand it upright.

**Align.** In Edit, **Align ▾** in the top bar lines up what's picked, as in a drawing program: left edges, centers on the vertical axis, right edges, top edges, centers on the horizontal axis, bottom edges. With three or more it also spaces them out: left edges, centers, right edges or tops, middles, bottoms at equal distances, or equal gaps between them across or down (the two outer ones stay put). **Relative to** says what they line up on: the last picked (the default), the first picked, the biggest, the smallest, the picked things taken together, or the whole plot. A group moves as one. Right-clicking a picked item shows the same choices.

**Arrange.** Things drawn later sit on top. Right-click an item for **Bring to front**, **Bring forward** (up one), **Send backward** (down one) and **Send to back**. They act on everything picked when the item is one of them, or on its group.

**Groups.** Pick several items, right-click one and choose **Group these**: from then on, picking any of them picks them all, so they move, arrange and are removed together. **Ungroup** splits them up again. To resize one of them, ungroup first.

**Links.** Text, icons and pictures can stand in for a fixture or a pool object. Right-click one → **Link to a fixture or pool object…** and type what it stands for: `101` or `Fixture 21.3`, `Group 3`, `Preset 4.1` (or `Color.1`), `Sequence 2`, `Executor 1.201`, `Macro 1`, `Effect 2`, `View 3`, `Spread 1`, `RateMaster 1` or `Plot 2`. Tapping it then does exactly what tapping that fixture or tile does, and it gets a colored outline while the fixture is selected or the object is in use. **Unlink** makes it plain again. The demo's UPSTAGE label is linked to Group 2.

### Zoom

**+** and **−** zoom in and out, **Fit** shows everything, and Ctrl and the wheel zoom round the pointer; the wheel scrolls, and the middle button drags the sheet about. **Lock** fixes the zoom and position: +, −, Fit, the wheel and dragging leave it where it is, which keeps a plot from being knocked out of place during a show.

Each plot window keeps its own zoom, position and lock, for each plot it shows, and the view stores them: recall the view and the plot comes back framed the way it was stored. Two windows can show the same plot at different zooms.

### Right-click and the top bar

Right-click an empty part of the plot for **Edit**, **Add**, **Align** (in Edit), **Zoom in**, **Zoom out**, **Fit**, **Lock zoom and position** and **Hide the top bar**. Hiding the top bar (Edit, Add, the zoom buttons) gives the plot the whole window; right-click again to **Show the top bar**. Both are kept with the view, and the window's tab right-click has them too.

### The Plots pool and commands

Tap a plot in the **Plots** pool to show it in the plot windows (`Plot 2` does the same). A plot window normally shows the plot chosen last; right-click its tab to keep it on one plot. Store and tap an empty slot to make a plot; right-click a tile to rename it, add the selected fixtures, or delete it. On the command line: `Store Plot 2 "Truss"`, `Label Plot 2 "Name"`, `Copy Plot 2 At 3`, `Move Plot 2 At 4`, `Delete Plot 2`.

The demo show has a **Rig** plot of all its fixtures, with its groups, three colors, the wash executor and a macro.

## Macros {#macros}

![The Macros pool: the demo show's three macros, colored. Macro 2 is running and waiting for a tap (Go)](images/macros.png)

A **macro** is a list of commands that runs line by line: anything you can type on the command line. Add the **Macros** window with **+ → Macros**.

- **Make one**: press **Store** and tap an empty slot (or right-click it → **New macro…**, or type `Store Macro 4 "Name"`). The macro editor opens.
- **Run it**: tap it, or type `Macro 4` (or `Go Macro 4`). A running macro is outlined and shows the line it's on.
- **Stop it**: right-click → **Stop**, or `Off Macro 4`. `Off Macro` stops every macro.
- **Edit it**: right-click → **Edit lines…**. Right-click also renames it, gives it a **Color…** (or `Appearance Macro 4 "Orange"`), switches it between foreground and background, and deletes it.

![The macro editor](images/macro-editor.png)

### Lines

| Column | Meaning |
| --- | --- |
| **Command** | The command, as you'd type it. It edits like the command line: with shortcuts off, keywords complete as you type; with shortcuts on, the shortcut keys type their keywords (S Store, G Group, T Thru, A At…), except inside quotes. |
| **Then** | **Follow**: the next line runs after the **Time** (0: straight away). **Wait**: the macro stops there until you tap it again (or `Go Macro 4`); then it goes on from the next line. |
| **Time** | Follow only: seconds before the next line. Tapping the macro during a Follow time goes on at once. |
| **On** | Off: the line is skipped. |
| **Run** | Off: the command is left on the command line for you to finish and press Enter, and the macro goes on. (A background macro runs it anyway.) |
| **Note** | For whoever edits the macro. |

**Add last command** adds the last command from the command history, so you can try a command, then keep it. **↑ ↓** reorder the lines.

### Foreground and background

- **Foreground** (the default): each line shows on the command line as it runs, as if typed, and goes into the command history.
- **Background**: the lines run out of sight, without touching the command line, so you can keep typing while it runs. They still go into the command history, marked with the macro (**BG** on the tile).

In the command history, lines a macro ran show which macro ran them.

### Inside a line

- **(Prompt)**: text in brackets asks for input when the line runs, and what you type takes its place: `Fixture (Which fixtures?) At 50`. Cancelling stops the macro.
- **@ at the end** leaves the line on the command line for you to finish (like Run off): `Store Cue @`.
- **@ at the start** puts what's already on the command line in front of the line: `@ At 100` after typing `Group 2`.
- **Variables**: `SetVar $level = 70` keeps a value; `$level` in any later line (or anything you type) is replaced by it: `Group 1 At $level`. Variables last until Lumora closes.
- **Colors**: `Appearance Sequence 2 "Red"` or `Appearance Macro 4 "#ff8800"` recolors a sequence or macro (`Off` takes the color away). Names are the standard colors: Red, Orange, Yellow, Lime, Green, Sea green, Cyan, Sky blue, Blue, Purple, Magenta, Pink, White.
- **Other macros**: `Go Macro 5` starts macro 5. A macro's own number as its last line (with a Follow time) makes it loop until you stop it.

The demo show has three: **Blue pars** (foreground), **Wash, tap for next** (background, with Waits: tap it to step through executor 201's cues) and **Profiles in**.

## Playback monitor {#playback-monitor}

![The Playback monitor](images/playback-monitor.png)

**+ → Playback monitor** shows a cue list as it runs: the live cue lit, the next one marked, and a progress bar while a cue fades in. It follows the sequence the last **Go** went to (from any executor, a Go button, a Follow or the command line), so it shows whatever you just ran; right-click its tab to lock it to one sequence and choose its columns (cue, label, fade, delay, notes, contents). Drag a column heading to reorder them. Double-click or right-click a cell to edit a cue's label, notes, fade or delay, or to go to that cue.

# The workspace

## Views and windows {#views}

### Views

A **view** is a saved layout of windows. The rail lists the show's views; tap one to show it. Views are stored in the show file, like a console's screens.

- Change the windows of a view as much as you like. Tapping the view again brings back what was stored. A changed view shows in italics with a dot. Going to a different view drops the changes too: the view you left is back to what was stored, unmarked, so store changes you want to keep before you leave.
- To keep the changes, press **Store** (the views light up red) and tap the view, or right-click it → store the changes, or type `Store View 2`.
- **+ New view** makes an empty view. With Store on, it makes one from the windows on screen.
- Right-click a view to recall, rename or delete it.
- A view also remembers each window's own state: which preset pool tab shows, a pool's scroll position, the fixture sheet's columns and open fixtures, and so on.
- **+ → Views** adds a Views pool window: tap a tile to show that view, or Store and tap a slot.

On the command line: `View 2`, `Store View 5 "Busking"`, `Label View 2 "Name"`, `Move View 2 At 6`, `Copy View 2 At 7`, `Delete View 5`.

### Windows

**+** in the top bar (or a double-click on empty space) adds a window to the current view:

| Window | Shows |
| --- | --- |
| **Color picker** | Gels, colors, temperature and gamut. |
| **Command history** | Every command and edit this session. |
| **Command keys** | The on-screen command keys. |
| **DMX monitor** | 512 channels of a universe. |
| **Effect editor** | One effect's layers and preview. |
| **Effects** | The effects pool and running effects. |
| **Encoders** | Encoders with their own feature and page (see [Encoder windows](#encoders)). |
| **Executor grid** | Executors as a grid, like a pool. |
| **Executor pages** | The executor pages as a pool. |
| **Executors** | A row of executors. |
| **Fixtures** | The fixture sheet (table or tiles). |
| **Groups** | The groups pool. |
| **Park** | Parked fixtures and attributes. |
| **Patch**, **DMX outputs**, **MIDI** | The setup screens as windows. |
| **Playback monitor** | A running cue list: the last one started with Go. |
| **Macros** | The macro pool. |
| **Plot** | One plot: fixtures live, pool objects to tap, text and pictures. |
| **Plots** | The plot pool. |
| **Presets** | A preset pool, with tabs for each feature. |
| **Rate masters** | The ten effect rate masters. |
| **Sequence editor** | The selected sequence's cues and blueprint. |
| **Sequences** | The sequence pool. |
| **Spreads** | The spreads pool and the active spread. |
| **Views** | The views as a pool. |

They're listed alphabetically, here and in the **+** menus.

### Arranging windows

Windows sit on a grid, and never overlap. Drag a window's header to move it; drag an edge or corner to resize it. Every window is a stack of tabs: drag a tab onto another window to stack it there, or onto empty space to give it a window of its own. A window's tab says what it shows ("Dimmer presets", "Faders 201–210", "Sequence editor · Pars").

### Window options

Right-click a window's tab for its options:

- **Pools** (groups, presets, effects, spreads, views, sequences): **Grid** sets tiles wide × tall (0 × 0 fits as many as there's room for), the **tile size**, and **Lock position**. The top left of every pool has ▲, the slot in the top-left corner (type a number to jump there), and ▼. Scrolling moves a row at a time.
- **Fixtures**: **Table** or **Tiles** layout, and which fixtures to show (all, patched, selected, or in the programmer).
- **Executors** and **Executor grid**: executors wide × tall, which row or first executor.
- **Playback monitor**: sequence (or follow the last Go), columns, banner.

Options are saved per window, so two windows of the same kind can look different.

## The fixture sheet {#fixture-sheet}

The **Fixtures** window lists every fixture with its current values by feature: ID, Name, Patch (address), Dimmer, Position, Gobo, Color, Beam, Focus and Control.

- **Coral** values are in the programmer; white values are away from their default; grey values are at rest.
- Values show as **preset names** wherever they come from a preset, in the programmer or from a running cue. While a cue is fading, or a master pulls the level down, you see the number instead.
- **Effects show by name** the same way, in the feature they move: a dimmer chase reads "Chase" in the Dimmer column, and an effect over a preset reads "Blue · Rainbow". Effects from the programmer and from running cues both show.
- **Click rows** to add fixtures to the selection; click again to take them out.
- **Drag a column heading** to reorder columns; drag the line at a heading's right edge to resize. The order is kept with the view.
- **Subfixtures** nest under their fixture with an arrow (see [Subfixtures](#subfixtures)).

![The fixture sheet with a chase running on the pars: their Dimmer column reads "Chase"](images/fixture-sheet-effects.png)
![Fixture tiles, with pixel bar 21 open](images/fixture-tiles.png)

- **Tiles** layout (right-click the tab) shows each fixture as a tile with its level, color, gobo and position. Fixtures with subfixtures get an arrow in the corner; open one and its cells sit with it on a tray, smaller, so they read as one fixture.

## Screens {#screens}

The **▣ 1** button next to **Show** opens screens 1–6. Pick a number to open another Lumora window (to put on a second monitor) or bring it to the front. Every screen shows the show's views by itself, so screen 2 can show Playback while screen 1 shows Program. The setup pages, encoder bar and show menu live on screen 1, and closing screen 1 closes the others. Without the encoder bar, screens 2–6 have four more rows of window grid (20 instead of 16), so a view made there has room for more windows; on screen 1 a view taller than 16 rows is squeezed to fit. Screens that were open reopen next time. Keys go to the command line of the screen in front.

## Command history {#history}

**+ → Command history** lists every command run this session (typed, keyed or tapped) with its result, and the edits made in windows (stores, renames and so on). Double-click a command to put it back on the command line; right-click to run it again or copy lines. Its tab options filter it to commands or edits.

## Interface settings {#interface}

**Show → Interface settings…** sets the interface size (bigger for touch screens), how many faders an executor page has, and how many encoders a page of the encoder bar has (five unless you change it; fewer show when they don't fit).

# Reference

## Command line reference {#command-reference}

### How commands work

- Keywords are case-insensitive, and common abbreviations work: `F` (Fixture), `G` (Group), `T` (Thru), `P` (Preset), `Seq`, `Exec`, `@` (At), `BP` (Blueprint).
- A bare number means Fixture: `5 At 50`.
- Presets are written by pool name: `Color.3`, `Position.12`, `All.2` (or `Preset 4.3`).
- Names go in double quotes: `Store Group 3 "Front"`.
- Options start with a slash: `/overwrite`, `/universal`, `/global`, `/selective`.

### Selection

```
Fixture 1 Thru 10 - 5           select 1 to 10 without 5
1 + 3 + 5                       several fixtures
Group 2                         a group (adds to the selection)
Fixture 101.3                   a subfixture (also Fixture 101 Cell 3)
Fixture 101.1 Thru 8            cells 1 to 8 of fixture 101
Fixture 101 Thru 104 Cell 2     cell 2 of each
Fixture 101.1.1 Thru 101.4.1    the first part of every head
Clear / ClearAll                deselect, then empty the programmer / both
Next / Prev                     one fixture of the selection at a time (Page Down / Up)
All                             the whole selection again
Last                            the selection before the last Clear (Ctrl+L)
Highlight                       toggle highlight
```

### Values

```
At 50                           intensity
At Full / At Zero / At At       full, zero, full
At Color.3                      apply a preset (also just Color.3)
Attribute Zoom At 30            any attribute, in its own values (% or its range)
Attribute Pan At -90            pan and tilt in degrees
Fixture 5 Attribute Pan At 45   select and set in one go
Flip                            same direction from the other side
Default / Default All           defaults into the programmer
Remove                          drop the selection's values
```

### Park

```
Park                            everything on the selection
Fixture 101 Park                fixture 101 (also Park Fixture 101, 101 Park)
Fixture 101 Intensity Park      one feature: Intensity, Position, Gobo, Color…
Attribute Pan Park              one attribute of the selection
Park Group 2 Position           position of a group
Unpark Fixture 101              the same forms let go
Unpark All                      everything
```

### Storing and editing

```
Store Group 3 "Front"
Store Color.1 "Red" /overwrite
Store Color.3 /selective        store one layer
Store Cue 5 / Store Cue 1 Thru 40 / Store Sequence 2 Cue 1.5
Store Default [/overwrite] / Delete Default
Store View 5 "Busking"
Store Spread 3 "Evens"
Update / Update Cue 3 / Update Color.1
Label Color.1 "Deep red"        also Group, Sequence, Cue, Fixture, Page, View, Effect, Spread
Label Group 3                   no name: a dialog asks for one
Label                           name the selected fixtures (numbered on)
Delete Cue 3 / Delete Group 2 / Delete Color.1 / Delete Spread 3
Copy Group 1 At 5 / Move Color.1 At Color.9 / Copy Cue 2 At 5 / Move Sequence 1 At 4
Copy Color.1 Thru 10 At Color.15  a run of presets (Move too; Thru Color.10 works as well)
```

### Cues and blueprints

```
Cue 3 Fade 4 / Cue 3 Delay 1
Cue 3 Fade 0 Thru 5 / Cue 3 Delay Off      individual times across the cue's fixtures
Fixture 1 Thru 8 At 100 Fade 0 Thru 4      a value with individual fades
Fade 0 Thru 4 / Delay 0 Thru 2 Thru 0 / Time 3 / Fade Off
Cue 3 Note "Wait for the actor"
Blueprint Cue 2 Group 1 Dimmer.1 Color.2
Blueprint Cue 2 Group 1 At 80 Color.2 Every 2 Offset 2 Spread 3 Effect 12 "Name"
Build Cue 2
Remove Fixture 3 Thru 5 Cue 2
```

### Playback

```
Go Executor 201 / GoBack Executor 201 / Off Executor 201 / Pause Executor 201
Top Executor 201
Goto Cue 3 Executor 201
Executor 201 At 75
Assign Sequence 2 At Executor 202
Assign Flash At Executor 201
Assign XFade At Executor 201
Executor 401 Button             also Encoder, Fader
Page 2 / Page + / Page -
Label Page 2 "Verse"
Blackout
```

### Effects and spreads

```
Effect 12                       run on the selection
Effect 12 Rate 150 Size 50      adjust (percent)
Effect 12 Off / Effect Off
Spread 3 / Spread Off
Spread Every 2 Offset 2 Blocks 2 Groups 4 Wings 2
Spread Phase -360 Thru 0 Direction Backward
RateMaster 2 At 128             set a rate master (%, BPM or ratio)
RateMaster 2                    tap its tempo
Assign RateMaster 1 At Executor 210
Assign Effect 3 At RateMaster 2
Label RateMaster 2 "Chase"
RateMaster 4 Button / RateMaster 4 Fader
```

### Plots

```
Plot 2                                       show it in the plot windows
Store Plot 2 "Truss" / Label Plot 2 "Name" / Copy Plot 2 At 3 / Move Plot 2 At 4 / Delete Plot 2
Assign Fixture 1 Thru 8 At Plot 2            fixtures and subfixtures (21.1 Thru 21.8)
Store Fixture 1 Thru 5 At Plot 3             the same as Assign
Assign Group 3 At Plot 2                     or Preset 4.1, Sequence 2, Executor 1.201, Macro 1, Effect 5…
Store Group 3 At Plot 2                      the same as Assign
```

### Macros

```
Macro 3 / Go Macro 3            run it, or go on from a Wait
Off Macro 3 / Off Macro         stop it, or all of them
Store Macro 3 "Name" / Label Macro 3 "Name" / Copy Macro 3 At 5 / Move Macro 3 At 5 / Delete Macro 3
Appearance Macro 3 "Orange" / Appearance Sequence 2 "#ff0000" / Appearance Macro 3 Off
SetVar $name = 50               then $name in any command
```

### Views

```
View 2 / Store View 2 / Label View 2 "Name" / Move View 2 At 6 / Copy View 2 At 7 / Delete View 5
```

### Tapping to finish a command

Commands can finish by tapping, as on most consoles. Press **Move**, tap a preset, then tap the slot it should go to. The same works for **Copy**, **Delete**, **Update**, **Remove**, **Label** (asks for the name) and **Assign** (tap a sequence, then an executor). Type `Assign Flash At` and tap an executor's button to give that button the function, or tap its fader or knob for a fader function. Moving something keeps the cues, blueprints and effects that use it pointing at it.

## Keyboard shortcuts and the command keys {#shortcuts}

![The command keys](images/keypad.png)

**Keypad** (**Ctrl+K**) floats the on-screen command keys over the view; **+ → Command keys** puts them in a view as a regular window. Every key shows its keyboard shortcut underneath.

| Key | Does | Key | Does |
| --- | --- | --- | --- |
| **S** | Store | **G** | Group |
| **U** | Update | **P** | Preset |
| **Del** | Delete | **Shift+S** | Sequence |
| **R** | Remove | **Q** | Cue |
| **C** | Copy | **E** | Executor |
| **M** | Move | **Shift+P** | Page |
| **L** | Label | **Shift+E** | Effect |
| **Shift+A** | Assign | **Space** | Go |
| **F** | Fixture | **Shift+Space** | Go Back |
| **T** | Thru | **Shift+G** | Goto |
| **A** | At | **O** | Off |
| **Shift+F** | Full | **H** | Highlight |
| **Shift+Z** | Zero | **Shift+B** | Blackout |
| **D** | Default | **Esc** | Clear |
| **Enter** | Run the command | **Shift+Esc** | Clear All |
| **Backspace** | Delete a word or digit | **F1** | This guide |
| **Page Down** | Next fixture of the selection | **Page Up** | Back a fixture |
| **Alt+D** | `Dimmer.` (type the number next) | **Alt+P** | `Position.` |
| **Alt+G** | `Gobo.` | **Alt+C** | `Color.` |
| **Alt+B** | `Beam.` | **Alt+F** | `Focus.` |
| **Alt+O** | `Control.` | **Alt+A** | `All.` |
| **Ctrl+L** | Select last | | |

The Alt keys put a preset pool on the command line ready for its number, so **Alt+C** then **3** is `Color.3`; they work while you're typing a command too. Digits, `.`, `+` and `-` type themselves. Shortcuts pause while you're typing in a text field (naming something, typing a command, a number box), and **Insert** turns them on and off; the **Shortcuts** button in the top bar shows which. Clicking anything that isn't a text field ends typing so shortcuts work again.

## Show files {#show-files}

**Show → New show**, **New demo show**, **Open…**, **Save** and **Save as…** manage shows. Shows are saved in the **Lumora** folder in your **Documents** (Lumora makes it), named after the show, unless you pick somewhere else; **Open…** starts there too. A show is a single file holding everything about the show:

- The patch and every fixture type it uses (so it opens the same on any computer).
- Groups, presets, spreads, sequences, cues, blueprints, effects and shapes.
- Executors, pages and their names.
- Views, each window's state, and the view on each screen.
- MIDI mappings, faders per page, and whether keyboard shortcuts are on.
- Stored defaults.

Lumora keeps only this on the computer itself:

- **DMX outputs and their network interface**: the outputs and the defaults for new universes.
- **Window placement and screens**, and the interface size, since these depend on the monitors.
- **The last show**, so it reopens at startup.
- **Fixture library details**: your GDTF Share user name and the downloaded-file cache.

Shows saved by older versions of Lumora open in newer ones and are brought up to date when they're opened (pan and tilt values become degrees, recipe lines become blueprint lines, and so on).

## Troubleshooting {#troubleshooting}

### The lights don't respond

1. **Is anything being sent?** Open **Outputs** and look at the **DMX monitor** for the universe. If the channels move when you set values, Lumora is sending.
2. **Is the output on**, on the right network card, and sending as the universe your node expects? Try the other *Art-Net 0 / Art-Net 1* setting.
3. **Grand master and blackout**: GM at 100% and BO off.
4. **Address and mode**: the fixture's address and mode (set on the fixture) must match the patch.
5. **Intensity**: moving heads and many LEDs need their dimmer *and* their shutter open. Check the Beam encoders.

### A moving head goes the wrong way

Tick **Invert pan** or **Invert tilt** in the patch.

### Colors look wrong

Check the fixture's mode, and whether it has a dimmer channel. RGB fixtures without one get a virtual dimmer: raise intensity as well as the color.

### Lumora doesn't start, or the installer won't run

Run the installer again and choose **Repair**, or uninstall Lumora and install it again; your shows and settings are kept. If Windows blocks the installer, click **More info**, then **Run anyway**.

### My MIDI controller isn't seen

Plug it in, then press **Rescan devices** on the MIDI screen. Check **Incoming** while moving a control. Make sure no other program has the device open.

### Something in a cue disappeared

A cue or blueprint line with a red or orange dot uses a group, preset, effect or spread that was deleted. Hover over the dot for details; store a new one at the same number to fix it.

## Glossary {#glossary}

| Term | Meaning |
| --- | --- |
| **Attribute** | One controllable property of a fixture: Dimmer, Pan, Red, Gobo1, Zoom… |
| **Blueprint** | A cue's description as lines of "this group, these presets", worked out live. |
| **Build** | Turning a cue's blueprint into ordinary cue values. |
| **Cell** | A subfixture of a pixel fixture. |
| **Cue** | A stored look, with fade and delay times. |
| **Default** | Where an attribute rests when nothing plays. |
| **Effect** | Values that move by themselves: chases, waves, circles. |
| **Executor** | An on-screen (or MIDI) fader, button or knob that plays a sequence. |
| **Individual times** | Fade and delay times of their own on some values of a cue, spread across the fixtures. |
| **Macro** | A list of commands that runs line by line, with Follow times or Waits for a tap. |
| **Plot** | A sheet with fixtures showing their intensity and color, pool objects to tap, text and pictures. |
| **Feature** | A family of attributes: Dimmer, Position, Gobo, Color, Beam, Focus, Control. |
| **Fixture type** | The description of a fixture's channels (a profile or personality). |
| **GDTF** | General Device Type Format, a standard fixture file format. |
| **Group** | A stored selection of fixtures. |
| **HTP** | Highest takes precedence: how intensity combines among HTP-priority sequences. |
| **LTP** | Latest takes precedence: the default priority, for every attribute. |
| **Off when overridden** | A sequence turns itself off once others have taken all its values. |
| **Priority** | Low, LTP, HTP or Ultra: which sequence wins. |
| **Park** | Holding attributes at their current output, over everything else. |
| **Patch** | Which fixture is at which DMX address. |
| **Preset** | Named, referenced values for one feature. |
| **Programmer** | The values you're working on, on top of everything. |
| **Selection** | The fixtures you're about to change. |
| **Sequence** | A list of cues. |
| **Spread** | How a selection is thinned and how effects roll across it. |
| **Subfixture** | A part of a fixture with its own ID: a pixel, a head, a head's beam. |
| **Tracking** | Cues store only changes; values carry on until changed. |
| **View** | A saved layout of windows. |
| **Virtual channel** | A channel that's programmed but not sent as DMX, such as a virtual dimmer. |

## What's new {#whats-new}

### a.7.2.1

- The user guide, README and interface text are tidied up and describe Lumora on its own terms.
- The Playback monitor window has a new internal name; views saved by earlier versions still open it.

### a.7.2.0

- **Lumora's source is on GitHub**: [github.com/typoirl/Lumora](https://github.com/typoirl/Lumora). This release collects everything up to a.7.1.14 (below): plots, macros, rate masters, individual fade and delay times, MIDI encoders, and the built-in Chauvet library with gobo pictures.

### a.7.1.14

- **Encoders on MIDI**: knobs and endless encoders map to the encoder bar by place, so the first one turns Pan on Position and Dimmer on Dimmer. Learn MIDI and click an encoder, or right-click it for **MIDI learn: encoder** and **MIDI learn: push**; the push opens the encoder's ranges. Endless encoders and plain knobs are both understood (an **Endless** column on the MIDI screen). See [Encoders and MIDI](#encoders-midi).
- **Shift for fine**: hold Shift on the keyboard while turning a MIDI encoder for small steps, as when dragging or scrolling.
- **Five encoders a page** by default, changeable in **Interface settings**.
- **Hide the encoder bar** with the arrow in its corner, and add **Encoders** windows (**+ → Encoders**), each with its own feature, page and count.
- **Plots: align and distribute**: **Align ▾** in Edit (and the right-click menu) lines up picked things by their edges or centers, or spaces them out evenly, relative to the last picked, first picked, biggest, smallest, the picked things or the whole plot. Groups move as one.
- **Plots: zoom per window, kept in the view**: each plot window keeps its own zoom, position and lock, and recalling the view brings them back.
- **Plots: right-click an empty spot** for Edit, Add, Align, zoom, **Lock zoom and position** and **Hide the top bar** (the window's tab has them too).
- **Plots: Store works like Assign**: `Store Fixture 101 Thru 108 At Plot 1`, `Store Group 3 At Plot 1`. The Add menu reads **Selected fixtures** and **Pool object…**.

### a.7.1.13

- **Gobo pictures for the Chauvet moving heads**: 25 built-in Chauvet fixtures (Intimidator, Maverick and Rogue spots, beams and hybrids) now show their gobos on the encoders, in the fixture sheet and on gobo presets, taken from the manufacturers' GDTF files on GDTF Share.
- Fixture files in the Open Fixture Library format can carry gobo pictures of their own: a top-level `images` object (name to base64 PNG or SVG) and an `image` name on a wheel slot.
- **Plots: arrange, group and link.** Right-click an item in Edit for **Bring to front**, **Bring forward**, **Send backward** and **Send to back**. **Group these** joins picked items so they're picked and moved together (**Ungroup** splits them). Text, icons and pictures can be **linked** to a fixture or any pool object: tapping one acts like that fixture or tile, and it lights while that's in use. See [Plots](#plots).
- **A truss that grows**: stretching the truss icon adds sections instead of stretching them, and it stands upright when it's taller than it is wide.
- **New plot icons**: a moving wash, a moving profile and a conventional profile.

### a.7.1.12

- **Plots**: sheets you lay out with fixtures and subfixtures, pool objects, text, icons and pictures (SVG too, as icons or a background). Fixtures show their intensity and color live; tap them to add them to the selection or take them out, or drag a box round several. Pool objects work like their pool tiles. **+** and **−** zoom, and the zoom can be locked. A **Plots** pool, a **Plot** window and a Plot view, `Plot 2` and `Assign … At Plot 2` on the command line. See [Plots](#plots).

### a.7.1.11

- **Individual times in the Fade and Delay columns**: no separate columns. A cue whose values have individual times shows the range they span in its Fade or Delay (`0 thru 3`); typing a range there spreads new ones across the cue's fixtures, and typing one number gives the cue that time again. Blueprint lines have **Fade** and **Delay** of their own, spread across the line's fixtures (and its effects) in the group's order; they override the cue's times. Pressing **T** while typing a time types *thru*. See [Individual fade and delay times](#individual-times).
- **Spreads no longer hold times**: individual times live on the command line and in those columns. Shows that kept times in a spread move them onto the blueprint lines that used it.
- **Effects use spreads from the pool**: each effect references one, and every existing effect now points at the spread matching its layout. Pick it in the effect editor.
- **Relative effects**: Dimmer and Attributes layers can add to what's underneath, with points from -100 to 100, so an effect can take a running executor's level down. A plot draws the wave around zero. New library effect: **Dip**.
- **Color effects drive every emitter**: amber, orange, yellow, lime, mint, cyan, indigo, purple, magenta, warm and cool white, and hue/saturation on virtual-color fixtures; the color picker uses the same mix.
- **Playback monitor** (was Playback status) follows the last sequence started with Go unless it's locked.
- **Rate masters in the Effects view** of the demo show, under the effects pool.
- **Macro lines edit like the command line**: keywords complete, or shortcut keys type them when shortcuts are on.
- **The effect editor keeps its scroll position** when you change a point (picking a color on a color effect used to jump back to the top).
- **Chauvet fixtures built in**: the Chauvet DJ and Chauvet Professional ranges are in the Built in library.

### a.7.1.10

- **Individual fade and delay times**, spread across the selection in its order: `At 100 Fade 0 Thru 4`, `Delay 0 Thru 2 Thru 0`, `Time 3`, and `Cue 3 Fade 0 Thru 5` or a range typed in the cue list. See [Individual fade and delay times](#individual-times).
- **Spreads carry times**: a fade and delay range in a spread on a blueprint line times that line's fixtures, values and effects alike, in the spread's layout.
- **Temp buttons use the times**: in with the first cue's fade, out with the Off line's.
- **Macros**: a pool of command lists with colors, Follow times and Waits, foreground or background, popups, @, variables and Appearance commands. See [Macros](#macros).
- **Framing shutters** in the encoder bar: drag the blades over the beam, Ins + Rot or A + B, with Link and resets. A generic profile with framing shutters is built in, and the demo show has two. See [Framing shutters](#shapers).
- **The Rate masters window** is ten faders in one row, like the 200 executors; right-click one to make it a Learn button instead.

### a.7.1.9

- **Rate masters**: ten masters for effect speed, in Rate, BPM or Ratio mode, with tap tempo, reset and freeze. Assign them to executors and effects; there's a Rate masters window and `RateMaster` commands. See [Rate masters](#rate-masters).
- **More window grid on other screens**: the encoder bar stays on screen 1, and screens 2–6 get 20 rows of grid instead of 16.
- **Sequence numbers on executors**, small and grey beside the sequence name.
- **Shift+A, tap a sequence, tap an executor** assigns it, with the tap landing anywhere on the executor.
- **Standard colors** are now Red, Orange (between the old orange and amber), Yellow, Lime, Green, Sea green, Cyan, Sky blue, Blue, Purple (Congo and Violet together), Magenta, Pink and White, last. Lavender is gone. The demo show's color presets follow, plus a **3200K** warm white preset.
- **Fixture library**: a **Built in** tab for Lumora's own fixtures, and every library is grouped by manufacturer, opening as you search. New built-in fixtures for **Pangolin BEYOND** (FB3 4/8/12/16 ch, Mid 32 ch, Max 50 ch) and **Resolume Arena** (Composition and Layer from the DMX Auto Map).
- **More parameters** in the fixture editor: infinite pan and tilt, Orange, Mint and Purple emitters, additive Magenta and Yellow, Prism 2, Frost 2, framing blades, virtual strobe, hue/saturation, movement macros and more. Fixture files map continuous pan/tilt, a second prism or frost, and blades to them.
- **Edit several channels at once** in the fixture editor: select them and right-click a cell, for example to give red, green and blue the same cell.
- **Go on the last cue turns the sequence off** when the Off line's trigger is Go and wrapping is off.
- **Effects show in the fixture sheet** by name, like presets.

### a.7.1.8

- **Effects run in a layer of their own**: they move around whatever value wins underneath instead of fighting it. A circle on one executor and a position on another both keep running, and the circle goes round the new position. See [Effects and priority](#effect-priority).
- **A Low effect runs over an LTP preset.** A Low color effect keeps running when an LTP color preset comes in; a newer Low color effect takes over from it.
- **Off when overridden** judges a sequence with values by its values, and an effect-only sequence by its effects, so a position no longer turns off a circle, but an executor with a position and a circle still goes when a later position takes over.
- **Stomp**: calling a preset onto fixtures running a programmer effect on that feature stops the effect for those fixtures.
