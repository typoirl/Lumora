#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Takes the screenshots the manual uses, from a built Lumora, using its developer options.

    python3 docs/manual/make_screenshots.py build/lumora

Runs Lumora off screen (QT_QPA_PLATFORM=offscreen) once per picture, with the demo show, and
writes PNGs to docs/manual/images/. Crops are made with Pillow when it's installed.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'images')

# name: (arguments, extra environment, crop box or None)
SHOTS = {
    'overview': (['--page', '0', '--run', 'Fixture 1 Thru 8;At 80;Color.9'], {}, None),
    'sequences': (['--page', '1'], {}, None),
    'effects': (['--page', '2', '--edit-effect', '1'], {}, None),
    'effect-relative': (['--page', '2', '--edit-effect', '19'], {}, (700, 55, 1500, 735)),
    'playback': (['--page', '3', '--run', 'Go Executor 201'], {'LUMORA_SHOT_DELAY': '4000'}, None),
    'patch': (['--page', '5'], {}, None),
    'outputs': (['--page', '6'], {}, None),
    'midi': (['--page', '7'], {}, (0, 0, 1500, 850)),
    'osc': (['--page', '7'], {'LUMORA_CONTROL_TAB': '1'}, (0, 0, 1500, 850)),
    'selection-matrix': (['--page', '0', '--add-window', 'selectionmatrix', '--run',
                          'Fixture 1 Thru 4;NextY;Fixture 5 Thru 8;Spread X Every 2;Spread Y Every 2'], {}, (120, 310, 1500, 735)),
    'spread-modifier': (['--page', '0', '--add-window', 'spreadmodifier', '--run',
                         'Fixture 1 Thru 4;NextY;Fixture 5 Thru 8;Spread X Every 2;Spread Y Every 2'], {}, (120, 310, 1500, 735)),
    'store': (['--page', '0', '--store'], {}, None),
    'keypad': (['--page', '0', '--keypad'], {}, None),
    'encoders-position': (['--page', '0', '--run', 'Fixture 11 Thru 14;At Position.3'],
                          {'LUMORA_ENCODER_CATEGORY': '2'}, (0, 735, 1500, 895)),
    'encoders-dimmer': (['--page', '0', '--run', 'Fixture 1 Thru 4;At 60'], {}, (0, 735, 1500, 895)),
    'spreads': (['--page', '0', '--add-window', 'spreads', '--run', 'Fixture 1 Thru 8;Spread 4'], {},
                (120, 310, 1500, 735)),
    'playback-monitor': (['--page', '3', '--add-window', 'playbackmonitor', '--run', 'Go Executor 201'], {'LUMORA_SHOT_DELAY': '4000'}, None),
    'color-picker': (['--page', '0', '--add-window', 'color', '--run', 'Fixture 1 Thru 8'], {},
                     (120, 310, 1500, 735)),
    'fixture-sheet': (['--page', '0', '--run', 'Fixture 21.1 Thru 21.4 + 22.3;At 70'],
                      {'LUMORA_PANEL_OPTIONS': 'fixtures|{"show":2,"expanded":[21,22]}'},
                      (120, 310, 1500, 735)),
    'park': (['--page', '0', '--add-window', 'park', '--run', 'Fixture 1 Thru 4 At 60;Park;ClearAll;Fixture 11 Attribute Pan Park;ClearAll'],
             {}, (120, 310, 1500, 735)),
    'executor-grid': (['--page', '0', '--add-window', 'executorgrid'], {}, (120, 310, 1500, 735)),
    'fixture-tiles': (['--page', '0', '--run', 'Fixture 21.1 Thru 21.4;At 70'],
                      {'LUMORA_PANEL_OPTIONS': 'fixtures|{"layout":"tiles","expanded":[21]}'}, (120, 310, 1500, 500)),
    'pools': (['--page', '0'], {}, (120, 55, 1500, 310)),
    'top-bar': (['--page', '0'], {}, (0, 0, 1500, 52)),
    'rail': (['--page', '0'], {}, (0, 52, 120, 420)),
    'command-line': (['--page', '0', '--run', 'Fixture 1 Thru 4'], {}, (0, 893, 1500, 940)),
    'cue-list': (['--page', '1', '--run', 'Cue 4 Follow'], {}, (120, 55, 1500, 420)),
    'blueprint': (['--page', '1'], {}, (520, 470, 1500, 620)),
    'executors': (['--page', '3', '--run', 'Go Executor 201;Assign RateMaster 1 At Executor 206;RateMaster 1 At 150'],
                  {'LUMORA_SHOT_DELAY': '4000'}, (120, 55, 1500, 735)),
    'rate-masters': (['--page', '0', '--add-window', 'ratemasters', '--run',
                      'RateMaster 1 At 150;Assign Effect 1 At RateMaster 1;Label RateMaster 2 "Chase";RateMaster 3 At 50;RateMaster 4 Button'],
                     {}, (120, 310, 1500, 735)),
    'fixture-sheet-effects': (['--page', '0', '--run', 'Fixture 1 Thru 8;At 80;Color.9;Effect 1'], {},
                              (120, 310, 1500, 735)),
    'shaper': (['--page', '0', '--run', 'Macro 3;Attribute Blade2Rot At 70;Attribute BladeSystemRot At 60;Attribute Blade4A At 15'],
               {'LUMORA_ENCODER_CATEGORY': '5'}, (0, 735, 1500, 895)),
    'plot': (['--page', '4', '--run', 'Go Executor 201;Fixture 11 Thru 14;At 80;Color.1;Clear;Fixture 21.1 Thru 21.8;At 100;Color.9'],
             {'LUMORA_SHOT_DELAY': '3500'}, (120, 55, 1500, 735)),
    'macros': (['--page', '0', '--add-window', 'macros', '--run', 'Macro 2'], {}, (120, 310, 1500, 735)),
    'help': (['--page', '0', '--add-window', 'help'], {}, (120, 310, 1500, 735)),
    'fixture-library': (['--library-shot', '{file}', '--library-search', '1:spot'], {}, None),
    'fixture-library-builtin': (['--library-shot', '{file}', '--library-search', '-1:'], {}, None),
}
SPECIAL = {
    'fixture-editor': ({'LUMORA_EDITOR_SHOT': 'generic/moving-spot|{file}'}, ['--page', '0']),
    'preset-picker': ({'LUMORA_PICKER_SHOT': '{file}'}, ['--page', '0']),
    'macro-editor': ({'LUMORA_MACRO_SHOT': '2|{file}'}, ['--page', '0']),
    'goto': ({'LUMORA_DIALOG_SHOT': '{file}'}, ['--page', '3', '--run', 'Go Executor 201;Go Executor 201;Goto Executor 201']),
    'link-picker': ({'LUMORA_LINK_SHOT': '{file}'}, ['--page', '0']),
    'preset-builder': ({'LUMORA_OPEN_BUILDER': '1', 'LUMORA_DIALOG_SHOT': '{file}'}, ['--page', '0']),
}


def run(binary, args, env_extra, file):
    env = dict(os.environ)
    env['QT_QPA_PLATFORM'] = 'offscreen'
    env.setdefault('LUMORA_SHOT_DELAY', '2500')
    env['LUMORA_WINDOW_SIZE'] = '1500x940'   # the size the crops are for
    env.update({k: v.replace('{file}', file) for k, v in env_extra.items()})
    own = any('{file}' in a for a in args)   # the option saves its own picture
    cmd = [binary, '--demo'] + [a.replace('{file}', file) for a in args]
    if not own and not any(k in env_extra for k in ('LUMORA_EDITOR_SHOT', 'LUMORA_PICKER_SHOT', 'LUMORA_MACRO_SHOT', 'LUMORA_DIALOG_SHOT', 'LUMORA_LINK_SHOT')):
        cmd += ['--screenshot', file]
    subprocess.run(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=90)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    binary = os.path.abspath(sys.argv[1])
    os.makedirs(OUT, exist_ok=True)
    try:
        from PIL import Image
    except ImportError:
        Image = None
    with tempfile.TemporaryDirectory() as tmp:
        jobs = [(n, a, e, c) for n, (a, e, c) in SHOTS.items()] + [(n, a, e, None) for n, (e, a) in SPECIAL.items()]
        for name, args, env, crop in jobs:
            raw = os.path.join(tmp, name + '.png')
            run(binary, args, env, raw)
            if not os.path.exists(raw):
                print('missing', name)
                continue
            dest = os.path.join(OUT, name + '.png')
            if Image:
                im = Image.open(raw)
                if crop:
                    im = im.crop(crop)
                im.save(dest, optimize=True)
            else:
                os.replace(raw, dest)
            print('wrote', dest)
    return 0


if __name__ == '__main__':
    sys.exit(main())
