#!/usr/bin/env python3
"""Transpile the GameMaker project into C++ sources + data files.

usage: build.py <GameMaker Studio 1.4 dir> <out dir>
"""
import os, re, sys, json
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(__file__))
from gml import parse, GMLError
import cgen
from cgen import Ctx, gen_function, collect_globalvars, cstr
import gmbuiltins as B

SRC = sys.argv[1]
OUT = sys.argv[2]
GEN = os.path.join(OUT, 'gen')
DATA = os.path.join(OUT, 'data')
os.makedirs(GEN, exist_ok=True)
os.makedirs(DATA, exist_ok=True)


def names(dirname, suffix):
    d = os.path.join(SRC, dirname)
    return sorted(f[:-len(suffix)] for f in os.listdir(d) if f.endswith(suffix))


objects = names('objects', '.object.gmx')
sprites = names('sprites', '.sprite.gmx')
sounds = names('sound', '.sound.gmx')
backgrounds = names('background', '.background.gmx')
fonts = names('fonts', '.font.gmx')
scripts = names('scripts', '.gml')
rooms_sorted = names('rooms', '.room.gmx')
# room order from project
proj = open(os.path.join(SRC, 'HotlineMiami.project.gmx'), encoding='utf-8', errors='replace').read()
rooms = [m.group(1) for m in re.finditer(r'<room>rooms\\([^<]+)</room>', proj)]
assert set(rooms) == set(rooms_sorted), 'room list mismatch'

for lst in (objects, sprites, sounds, backgrounds, fonts, scripts, rooms):
    for n in lst:
        assert re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', n), f'bad name {n}'

# ---------------------------------------------------------------- read all code
obj_events = {}   # obj -> {(type,num,other): code}
obj_props = {}
nonclode = []
for on in objects:
    t = ET.parse(os.path.join(SRC, 'objects', on + '.object.gmx')).getroot()
    props = {k: t.findtext(k) for k in ('spriteName', 'solid', 'visible', 'depth', 'persistent', 'parentName', 'maskName')}
    obj_props[on] = props
    evs = {}
    for ev in t.iter('event'):
        key = (int(ev.get('eventtype')), int(ev.get('enumb') or 0), ev.get('ename') or '')
        code = evs.get(key, '')
        pending = None
        for a in ev.findall('action'):
            kind = int(a.findtext('kind'))
            libid, aid = a.findtext('libid'), a.findtext('id')
            argel = list(a.iter('argument'))
            args = [(x.findtext('string') if x.findtext('string') is not None else (x.findtext('script') or '')) for x in argel]
            piece = None
            if kind == 7 and libid == '1' and aid == '603':
                piece = (args[0] if args else '') + '\n'
            elif libid == '1' and aid == '601':   # execute script
                sa = [x for x in args[1:] if x != '0']
                piece = f'{args[0]}({",".join(sa)})\n' if args and args[0] else ''
            elif libid == '1' and aid == '612':   # if variable (guards the next action)
                op = {'0': '=', '1': '<', '2': '>'}.get(args[2], '=')
                pending = f'({args[0]}){op}({args[1]})'
                continue
            else:
                nonclode.append((on, key, args))
                continue
            if pending is not None:
                piece = f'if ({pending}) {{\n{piece}}}\n'
                pending = None
            code += piece
        evs[key] = code
    obj_events[on] = evs

script_src = {n: open(os.path.join(SRC, 'scripts', n + '.gml'), encoding='utf-8', errors='replace').read() for n in scripts}

room_data = {}
for rn in rooms:
    room_data[rn] = ET.parse(os.path.join(SRC, 'rooms', rn + '.room.gmx')).getroot()

# ---------------------------------------------------------------- globalvar discovery
gv = set()
for n, s in script_src.items():
    try:
        collect_globalvars(parse(s, n), gv)
    except GMLError as e:
        print('PARSE ERROR', n, e)
for on, evs in obj_events.items():
    for key, c in evs.items():
        try:
            collect_globalvars(parse(c, on), gv)
        except GMLError as e:
            print('PARSE ERROR', on, key, e)
print('globalvars:', sorted(gv))

ctx = Ctx(objects, sprites, sounds, backgrounds, fonts, rooms, scripts, [], gv)

# ---------------------------------------------------------------- generate C++
HEADER = '#include "../runtime/rt.h"\n#include "ids.h"\n#include "protos.h"\n\n'
parts = []   # list of (filename, text)
fn_names = {}  # (obj,type,num,other) -> cpp name
errors = []


def safe_gen(kind, owner, src, name, t=0, n=0):
    try:
        return gen_function(ctx, kind, owner, src, name, t, n)
    except Exception as e:  # noqa
        errors.append((name, repr(e)))
        if kind == 'event':
            return f'void {name}(Inst* self, Inst* other) {{ /* transpile error: {str(e)[:80]!r} */ }}\n'
        return f'Value {name}(Inst* self, Inst* other, Args args) {{ return Value(0.0); }}\n'


chunks = []
cur = []
cur_size = 0


def emit(text):
    global cur, cur_size
    cur.append(text)
    cur_size += len(text)
    if cur_size > 400_000:
        chunks.append(''.join(cur))
        cur, cur_size = [], 0


protos = []
for sn in scripts:
    emit(safe_gen('script', None, script_src[sn], f'S_{sn}'))
    protos.append(f'Value S_{sn}(Inst* self, Inst* other, Args args);')

ev_entries = {}
for on in objects:
    ents = []
    for (t, n, other), code in sorted(obj_events[on].items()):
        if not code.strip():
            continue
        nm = f'E_{on}_{t}_{n}' + (f'_{other}' if other else '')
        emit(safe_gen('event', on, code, nm, t, n))
        protos.append(f'void {nm}(Inst* self, Inst* other);')
        ents.append((t, n, other, nm))
    ev_entries[on] = ents

# rooms: creation code and instance creation code
room_fns = []
inst_code_fns = []
for rn in rooms:
    r = room_data[rn]
    code = (r.findtext('code') or '').strip()
    if code:
        nm = f'RC_{rn}'
        emit(safe_gen('event', None, code, nm))
        protos.append(f'void {nm}(Inst* self, Inst* other);')
        room_fns.append(nm)
    else:
        room_fns.append(None)
    for k, inst in enumerate(r.iter('instance')):
        c = (inst.get('code') or '').strip()
        if c:
            nm = f'IC_{rn}_{k}'
            emit(safe_gen('event', None, c, nm))
            protos.append(f'void {nm}(Inst* self, Inst* other);')
            inst_code_fns.append(nm)
            inst.set('_icode', str(len(inst_code_fns) - 1))

if cur:
    chunks.append(''.join(cur))
for i, c in enumerate(chunks):
    open(os.path.join(GEN, f'game_{i}.cpp'), 'w').write(HEADER + c)

# ---------------------------------------------------------------- ids.h
L = ['#pragma once', '#include "../runtime/builtins.h"', '']
for pre, lst in (('OBJ', objects), ('SPR', sprites), ('SND', sounds), ('BG', backgrounds), ('FNT', fonts),
                 ('RM', rooms), ('SCR', scripts)):
    L.append(f'enum {{ ' + ', '.join(f'{pre}_{n}={i}' for i, n in enumerate(lst)) + f', {pre}__COUNT={len(lst)} }};' if lst else f'enum {{ {pre}__COUNT=0 }};')
L.append(f'#define VAR_COUNT {len(ctx.varnames)}')
L.append('enum { ' + ', '.join(f'V_{n}={i}' for n, i in ctx.varnames.items()) + ' };' if ctx.varnames else '')
open(os.path.join(GEN, 'ids.h'), 'w').write('\n'.join(L) + '\n')
B.gen_header(os.path.join(OUT, 'runtime', 'builtins.h'))

# ---------------------------------------------------------------- tables.cpp
T = [HEADER]
open(os.path.join(GEN, 'protos.h'), 'w').write('#pragma once\n#include "../runtime/rt.h"\n' + '\n'.join(protos) + '\n')
T.append('')
T.append('const char* const g_obj_names[] = {' + ','.join(cstr(n) for n in objects) + '};')
T.append('const char* const g_spr_names[] = {' + ','.join(cstr(n) for n in sprites) + '};')
T.append('const char* const g_snd_names[] = {' + ','.join(cstr(n) for n in sounds) + '};')
T.append('const char* const g_bg_names[] = {' + ','.join(cstr(n) for n in backgrounds) + '};')
T.append('const char* const g_fnt_names[] = {' + ','.join(cstr(n) for n in fonts) + '};')
T.append('const char* const g_rm_names[] = {' + ','.join(cstr(n) for n in rooms) + '};')
T.append('const char* const g_var_names[] = {' + ','.join(cstr(n) for n in ctx.varnames) + (',' if ctx.varnames else '') + '""};')
T.append('const ScriptFn g_scripts[] = {' + ','.join(f'S_{s}' for s in scripts) + '};')
T.append('const char* const g_scr_names[] = {' + ','.join(cstr(n) for n in scripts) + '};')
T.append('const EvFn g_room_code[] = {' + ','.join(n or 'nullptr' for n in room_fns) + '};')
T.append('const EvFn g_inst_code[] = {' + ','.join(inst_code_fns + ['nullptr']) + '};')


def idx(table, name):
    return table.index(name) if name in table else -1


T.append('const ObjDef g_objs[] = {')
for on in objects:
    p = obj_props[on]
    parent = idx(objects, p['parentName']) if p['parentName'] != '<undefined>' else -1
    spr = idx(sprites, p['spriteName']) if p['spriteName'] != '<undefined>' else -1
    mask = idx(sprites, p['maskName']) if p['maskName'] != '<undefined>' else -1
    evs = ev_entries[on]
    evlist = ','.join(
        '{%d,%d,%d,%s}' % (t, n, idx(objects, other) if other else -1, nm) for (t, n, other, nm) in evs)
    T.append('  {%s,%d,%d,%d,%d,%d,%d,%d, %d, std::vector<EvDef>{%s}},' % (
        cstr(on), spr, mask, parent, 1 if p['solid'] == '-1' else 0, 1 if p['visible'] == '-1' else 0,
        int(p['depth']), 1 if p['persistent'] == '-1' else 0, len(evs), evlist))
T.append('};')
T.append(f'const int g_obj_count = {len(objects)};')
open(os.path.join(GEN, 'tables.cpp'), 'w').write('\n'.join(T) + '\n')

# ---------------------------------------------------------------- data files (resources, rooms)
def sprite_line(sn):
    t = ET.parse(os.path.join(SRC, 'sprites', sn + '.sprite.gmx')).getroot()
    frames = [f.text.replace('\\', '/') for f in t.iter('frame')]
    g = lambda k: t.findtext(k) or '0'
    return ' '.join(['SPR', sn, g('xorig'), g('yorigin'), g('bbox_left'), g('bbox_right'), g('bbox_top'),
                     g('bbox_bottom'), g('width'), g('height'), g('colkind'), g('bboxmode'), str(len(frames))] + frames)


D = []
for sn in sprites:
    D.append(sprite_line(sn))
for bn in backgrounds:
    t = ET.parse(os.path.join(SRC, 'background', bn + '.background.gmx')).getroot()
    D.append(' '.join(['BG', bn, t.findtext('width'), t.findtext('height'), t.findtext('istileset'),
                       t.findtext('tilewidth'), t.findtext('tileheight'), t.findtext('data').replace('\\', '/')]))
for fn_ in fonts:
    t = ET.parse(os.path.join(SRC, 'fonts', fn_ + '.font.gmx')).getroot()
    gl = [(g.get('character'), g.get('x'), g.get('y'), g.get('w'), g.get('h'), g.get('shift'), g.get('offset'))
          for g in t.iter('glyph')]
    D.append(' '.join(['FONT', fn_, t.findtext('size'), t.findtext('bold'), t.findtext('italic'), str(len(gl))] +
                      [' '.join(x) for x in gl]))
for sn in sounds:
    t = ET.parse(os.path.join(SRC, 'sound', sn + '.sound.gmx')).getroot()
    D.append(' '.join(['SND', sn, t.findtext('data') or '', t.findtext('volume/volume') or '1', t.findtext('kind') or '0']))
# datafiles
D.append('DATADIR datafiles')

for ri, rn in enumerate(rooms):
    r = room_data[rn]
    views = list(r.iter('view'))
    bgs = list(r.iter('background'))
    D.append(' '.join(['ROOM', rn, r.findtext('width'), r.findtext('height'), r.findtext('speed'), r.findtext('persistent'),
                       r.findtext('colour'), r.findtext('showcolour'), r.findtext('enableViews'), str(ri)]))
    for v in views:
        D.append(' '.join(['VIEW', v.get('visible'), v.get('xview'), v.get('yview'), v.get('wview'), v.get('hview'),
                           v.get('xport'), v.get('yport'), v.get('wport'), v.get('hport'), v.get('hborder'),
                           v.get('vborder'), v.get('hspeed'), v.get('vspeed'), v.get('objName').replace('<undefined>', '-')]))
    for b in bgs:
        D.append(' '.join(['RBG', b.get('visible'), b.get('foreground'), b.get('name') or '-', b.get('x'), b.get('y'),
                           b.get('htiled'), b.get('vtiled'), b.get('hspeed'), b.get('vspeed'), b.get('stretch')]))
    for inst in r.iter('instance'):
        D.append(' '.join(['INST', inst.get('objName'), inst.get('x'), inst.get('y'), inst.get('scaleX'), inst.get('scaleY'),
                           inst.get('rotation'), inst.get('colour'), inst.get('_icode', '-1')]))
    for tl in r.iter('tile'):
        D.append(' '.join(['TILE', tl.get('bgName'), tl.get('x'), tl.get('y'), tl.get('w'), tl.get('h'), tl.get('xo'),
                           tl.get('yo'), tl.get('depth'), tl.get('scaleX'), tl.get('scaleY'), tl.get('colour'), tl.get('id')]))
    D.append('ENDROOM')
open(os.path.join(DATA, 'game.dat'), 'w').write('\n'.join(D) + '\n')

# ---------------------------------------------------------------- report
print('objects', len(objects), 'scripts', len(scripts), 'rooms', len(rooms), 'chunks', len(chunks))
print('transpile errors:', len(errors))
for e in errors[:20]:
    print('  ', e)
print('non-code actions skipped:', len(nonclode))
for x in nonclode[:12]:
    print('  ', x)
print('unresolved free identifiers (treated as instance vars): %d' % len(ctx.unknown_free))
json.dump(ctx.unknown_free, open(os.path.join(OUT, 'unknown_free.json'), 'w'), indent=0, sort_keys=True)
json.dump(ctx.builtin_calls, open(os.path.join(OUT, 'builtin_calls.json'), 'w'), indent=0, sort_keys=True)
print('builtin functions called:', len(ctx.builtin_calls))

# ---------------------------------------------------------------- funcs.h (declarations for every builtin used)
extra = ['ds_list_find_value', 'ds_list_replace']
allf = sorted(set(ctx.builtin_calls) | set(extra))
with open(os.path.join(OUT, 'runtime', 'funcs.h'), 'w') as f:
    f.write('// generated by tools/build.py\n#pragma once\n')
    for n in allf:
        f.write(f'Value F_{n}(Inst* self, Inst* other, Args a);\n')
