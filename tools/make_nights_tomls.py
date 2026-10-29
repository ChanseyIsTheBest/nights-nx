#!/usr/bin/env python3
"""nights-nx (2026-10-29): writes the tomls from banjo-nx's shipped renut.toml (night_gpu5, 2026-10-28,
dist/templates_banjo/renut.toml) filtered to this build's settings (tools/toml_filter_keys.py), with NiGHTS's values.
The user (2026-10-29): "the only thing I want in the .toml by default is" the controls, the language and SMAA; the
rest is compiled into the NRO (sa2-nx's built-in config):

  app/nights_switch_defaults.toml        the release settings built into the Switch NRO (src/renut_app.h
                                         BuiltInConfig): every exact enhancement as banjo-nx ships it, the release
                                         picture (SMAA, 1280x720 handheld, 1920x1080 docked), double buffering with
                                         its queue drain, the diagnostics and profilers off
  app/nights_switch.toml                 the player's renut.toml (the language, SMAA, the controls), written next to
  = dist/switch/nights-nx/renut.toml     the NRO when there is none; it overrides the built-in settings key by key
  dist/switch/nights-nx/configs/         renut.toml with one change each (the game's own picture, ...), and two whole
                                         files: renut_diagnostics.toml (the 10 s diagnostics on: the log to send
                                         back) and renut_ab.toml (the in-run A/B)
  dist/emu_base.toml                     the emulator checks: the diagnostics settings with captures every 15 s, the
                                         verify keys on, a scripted route (input_script) through the menus into
                                         Spring Valley

  python -I tools/make_nights_tomls.py
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(ROOT, 'dist', 'switch', 'nights-nx')

HEADER = '''# nights-nx (NiGHTS into Dreams..., XBLA) - settings. Copy this folder to sdmc:/switch/nights-nx/ with the game's
# extracted package in game_root/ beside nights-nx.nro (default.xex, default.xexp of the title update, afs/, cse/, ...).
# Made by tools/make_nights_tomls.py from banjo-nx's shipped renut.toml (2026-10-28): reNut's renderer on deko3d with
# every enhancement banjo-nx ships, the ones measured SLOWER ON THE CONSOLE off. The release picture (your choice,
# 2026-10-29): SMAA, 1280x720 handheld, 1920x1080 docked, double buffering; configs/ has the game's own picture
# (renut_exact.toml) and the other options.
'''

# NiGHTS's values (key -> value as toml text); None removes the key.
COMMON = {
    'license_mask': '1',               # the full XBLA game (your package's licence), not the trial
    'headless': 'true',                # the first boot's "No Game Data" box answers "Create Game Data" by itself
    'banjo_renderer': '"renut"',
    'renut_gpu_backend': '"deko3d"',
    'nights_renut_swap_pacing': 'true',
    # The release picture (the user's choice, 2026-10-29: "SMAA on 720p handheld 1080p docked"): SMAA before the HUD
    # (nights_renut_smaa_before_ps: NiGHTS has no final composite), the whole frame at 1280x720 handheld and 1920x1080
    # docked (the 3D and the HUD), no dynamic resolution.
    'banjo_renut_smaa': 'true',
    'nights_renut_smaa_before_ps': '"82E6A43836DB5864"',
    'banjo_renut_render_height_handheld': '720',
    'banjo_renut_render_height_docked': '1080',
    'banjo_renut_display_height_docked': '1080',
    'banjo_renut_dynamic_resolution': 'false',
    # Double buffering and its input-lag fix (the user's request, 2026-10-29, from futari-nx and sa2-nx).
    'nights_renut_double_buffer': 'true',
    'nights_renut_queue_drain': 'true',
    # N&B-only (fur shells, the photo portraits): no effect on NiGHTS.
    'banjo_renut_fur_shells_percent': None, 'banjo_renut_portrait_fur_shells_percent': None,
    'banjo_renut_portrait_msaa_samples': None, 'banjo_renut_fur_shells': None, 'banjo_renut_portrait_fur_shells': None,
    # No four-core mode (the user, 2026-10-29: "disable game from using fourth core only let system use"): core 3 is
    # left to the system even when a launch grants it, and no forwarder NPDM is ever written.
    'banjo_system_core': '0',
    'banjo_forwarder_program_id': '""',
    'banjo_renut_ab_targets': '""',
    # 1.0.1 (the user): no interpolation (nights_present_60) and no old 30 fps order (nights_30fps_original) in any
    # toml; both stay off as the NRO's defaults.
}
# The picture options (the user's decision), one config each in configs/: renut.toml with that change.
CONFIGS = {
    'renut_60fps.toml': ("60 frames a second of the game's own (one logic step a frame, the game's speed "
                         "unchanged) instead of 30", {'nights_true60': 'true'}),
    'renut_exact.toml': ("the game's own picture (pixel-identical): 1280x720 in both modes, no SMAA",
                         {'banjo_renut_smaa': 'false', 'banjo_renut_render_height_docked': '720',
                          'banjo_renut_display_height_docked': '720'}),
    'renut_no_smaa.toml': ('the release resolutions (720 handheld, 1080 docked) without SMAA',
                           {'banjo_renut_smaa': 'false'}),
    'renut_triple_buffer.toml': ('three swapchain images instead of two (the input-lag A/B: double buffering and its '
                                 'queue drain off)', {'nights_renut_double_buffer': 'false'}),
    'renut_banjo_resolution.toml': ("banjo-nx's resolutions: the 3D at 648 lines handheld, 864 docked (the HUD at "
                                    "1080), dynamic resolution; the 3D scaled up before the HUD (untested on NiGHTS)",
                                    {'banjo_renut_render_height_handheld': '648',
                                     'banjo_renut_render_height_docked': '864',
                                     'banjo_renut_dynamic_resolution': 'true'}),
}
# The player's renut.toml (app/nights_switch.toml): the user's list of 2026-10-29, and (1.0.1, the user) nights_true60
# off: the new 30 fps by default ("much much better camera framepacing"), the game's own 60 as the option.
PLAYER = '''# NiGHTS into Dreams... (nights-nx) settings, read at start. Everything else is built into nights-nx.nro; a setting
# written here (or a file of configs/ copied over this one) overrides the built-in one.

# Language: auto (the Switch's language), english, japanese, french, german, spanish or italian.
language = "auto"

# SMAA edge smoothing on the 3D (the HUD and text stay sharp). false = the game's own edges.
banjo_renut_smaa = true

# Frame rate. false = 30 (the game's two logic steps a frame, the camera paced evenly). true = 60 frames a second of
# the game's own (one logic step a frame; the game's speed is unchanged).
nights_true60 = false

# Face buttons by position, as on an Xbox pad (bottom = A). false = by letter.
input_xbox_layout = true

# Single Joy-Cons are held sideways, one per player.
input_joycon_sideways = true

# With input_joycon_sideways: the single Joy-Con's stick turned for the sideways grip.
input_joycon_sideways_stick = true

# Button mapping: the Switch buttons that press each Xbox button. Empty = default.
# Buttons: A B X Y L R ZL ZR PLUS MINUS LSTICK RSTICK UP DOWN LEFT RIGHT SL SR NONE. Example: a = "B, ZR"

[input_p1]
a = ""
b = ""
x = ""
y = ""
lb = ""
rb = ""
lt = ""
rt = ""
start = ""
back = ""
ls = ""
rs = ""
up = ""
down = ""
left = ""
right = ""
'''
PLAY = {
    'banjo_renut_diag_resolve_trace': 'false', 'banjo_renut_diag_gpu_shaders': 'false', 'renut_gpu_profile': '0.0',
    'switch_profile_stacks': 'false', 'banjo_stall_watch_seconds': '0', 'banjo_cpu_profiler': 'false',
    'banjo_show_fps': 'false', 'banjo_show_profiler': 'false', 'log_level': '"warning"',
}
AB = {
    # 30 s halves; 1 = banjo_renut_precise_barriers (Vulkan only: nothing changes on the Switch), the control.
    'banjo_renut_ab_s': '30',
    'banjo_renut_ab_targets': '"1,45,48,47,46,42,43,34,67"',
}
EMU = {
    'nfsmw_captura_cada_s': '15', 'nfsmw_captura_max': '40',
    'input_script': '"24:START,28:A,32:A,36:START,40:A,44:A,48:A,52:A,56:A,60:A,64:A,68:A,72:A,76:A,80:A,84:A,88:A"',
    'input_script_sticks': '75',
    'banjo_renut_fast_draw_verify': 'true', 'banjo_renut_resource_objects_verify': 'true',
    'banjo_renut_persistent_shared_verify': 'true', 'banjo_renut_snapshot_deltas_verify': 'true',
    'banjo_renut_store_cards_verify': 'true', 'banjo_renut_whole_buffers_verify': 'true',
    'banjo_renut_fetch_cache_verify': 'true', 'banjo_renut_chain_reuse_verify': 'true',
    'banjo_renut_record_extents_verify': 'true', 'banjo_renut_replay_group_pointers_verify': 'true',
    'banjo_deko3d_push_constants_verify': 'true', 'banjo_deko3d_txq_verify': 'true',
}


def apply(text, values):
    for k, v in values.items():
        pat = re.compile(r'^\s*%s\s*=.*$\n?' % re.escape(k), re.M)
        if v is None:
            text = pat.sub('', text)
        elif pat.search(text):
            text = pat.sub('%s = %s\n' % (k, v), text, count=1)
        else:
            m = re.search(r'^\[', text, re.M)
            line = '%s = %s\n' % (k, v)
            text = text[:m.start()] + line + text[m.start():] if m else text.rstrip('\n') + '\n' + line
    return text


def main():
    os.makedirs(DIST, exist_ok=True)
    filtered = os.path.join(ROOT, 'out', 'renut_toml_filtered.toml')
    subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools', 'toml_filter_keys.py'),
                    os.path.join(ROOT, 'dist', 'templates_banjo', 'renut.toml'), filtered], check=True)
    base = open(filtered, encoding='utf-8').read()
    base = re.sub(r'(?s)\A(\s*#[^\n]*\n)*', '', base)  # banjo-nx's own header
    shipped = HEADER + apply(base, COMMON)  # the diagnostics settings (configs/renut_diagnostics.toml)
    builtin = ('# The release settings built into the Switch NRO (tools/make_nights_tomls.py; src/renut_app.h '
               'BuiltInConfig).\n' + apply(shipped, PLAY))
    outs = {
        os.path.join(ROOT, 'app', 'nights_switch_defaults.toml'): builtin,
        os.path.join(ROOT, 'app', 'nights_switch.toml'): PLAYER,
        os.path.join(DIST, 'renut.toml'): PLAYER,
        os.path.join(DIST, 'configs', 'renut_diagnostics.toml'): (
            '# configs/renut_diagnostics.toml: the release settings with the 10 s diagnostics in the log (the log to '
            'send back after a test). Copy over renut.toml.\n' + shipped),
        os.path.join(DIST, 'configs', 'renut_ab.toml'): (
            '# configs/renut_ab.toml: the diagnostics settings with the in-run A/B (30 s halves; target 1 does nothing '
            'on the Switch: the control). Copy over renut.toml.\n' + apply(shipped, AB)),
        os.path.join(ROOT, 'dist', 'emu_base.toml'): '# emu_base.toml: the emulator checks (tools/emu_run.sh).\n'
                                                     + apply(shipped, EMU),
        # The PGO session (docs/pgo-lto.md): the release settings (every profiler and diagnostic off, as banjo-nx's
        # second profile), the copies called directly, the scripted route, a capture a minute.
        os.path.join(ROOT, 'dist', 'emu_pgo.toml'): '# emu_pgo.toml: the PGO session (tools/emu_run.sh).\n'
                                                    + apply(apply(shipped, PLAY), {
                                                        'input_script': EMU['input_script'],
                                                        'input_script_sticks': EMU['input_script_sticks'],
                                                        'banjo_hot_guard_calls': '0', 'banjo_hot_guard_period': '0',
                                                        'nfsmw_captura_cada_s': '60', 'nfsmw_captura_max': '10'}),
    }
    for name, (what, values) in CONFIGS.items():
        outs[os.path.join(DIST, 'configs', name)] = ('# configs/%s: renut.toml with one change for you to judge: %s. '
                                                     'Copy over renut.toml to try it.\n' % (name, what)
                                                     + apply(PLAYER, values))
    os.makedirs(os.path.join(DIST, 'configs'), exist_ok=True)
    for p, t in outs.items():
        open(p, 'w', encoding='utf-8', newline='\n').write(t)
        print('wrote', os.path.relpath(p, ROOT), sum(1 for l in t.splitlines() if re.match(r'^\s*\w+\s*=', l)), 'keys')


if __name__ == '__main__':
    main()
