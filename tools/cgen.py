"""AST -> C++ code generator for GML."""
import re
from gml import parse
from gmbuiltins import INST_BUILTINS, GLOBAL_BUILTINS, CONSTANTS

CPP_RESERVED = set('''alignas alignof and asm auto bool break case catch char class const continue default delete do
double else enum explicit export extern false float for friend goto if inline int long mutable namespace new
operator private protected public register return short signed sizeof static struct switch template this throw
true try typedef typename union unsigned using virtual void volatile while'''.split())


def cstr(s):
    out = []
    for b in s.encode('utf-8', 'replace'):
        c = chr(b)
        if c == '\\':
            out.append('\\\\')
        elif c == '"':
            out.append('\\"')
        elif c == '\n':
            out.append('\\n')
        elif c == '\r':
            out.append('\\r')
        elif 32 <= b < 127 and c != '?':
            out.append(c)
        else:
            out.append('\\%03o' % b)
    return '"' + ''.join(out) + '"'


def ident(n):
    return n


class Ctx:
    """Resource name tables shared across the whole project."""

    def __init__(self, objects, sprites, sounds, backgrounds, fonts, rooms, scripts, paths, globalvars):
        self.sets = {
            'OBJ': set(objects), 'SPR': set(sprites), 'SND': set(sounds), 'BG': set(backgrounds),
            'FNT': set(fonts), 'RM': set(rooms), 'PATH': set(paths),
        }
        self.scripts = set(scripts)
        self.globalvars = set(globalvars)
        self.varnames = {}  # dynamic var name -> id
        self.unknown_free = {}  # name -> count (dynamic variable names)
        self.builtin_calls = {}
        self.script_calls = set()

    def var_id(self, name):
        if name not in self.varnames:
            self.varnames[name] = len(self.varnames)
        return self.varnames[name]


class FnGen:
    def __init__(self, ctx, kind, owner, ev_type=0, ev_num=0, ev_other=None):
        self.ctx = ctx
        self.kind = kind  # 'event' or 'script'
        self.owner = owner
        self.ev_type = ev_type
        self.ev_num = ev_num
        self.locals = set()
        self.tmp = 0
        self.nsp = 0

    def t(self):
        self.tmp += 1
        return f'__t{self.tmp}'

    # ---------------- resource/identifier resolution
    def resolve_const(self, name):
        for pre, s in self.ctx.sets.items():
            if name in s:
                return f'{pre}_{name}'
        return None

    # ---------------- expressions
    def ex(self, e):
        k = e[0]
        if k == 'num':
            v = e[1]
            if v == int(v) and abs(v) < 1e15:
                return f'Value({int(v)}.0)'
            return f'Value({v!r})'
        if k == 'str':
            return f'Value({cstr(e[1])})'
        if k == 'id':
            return self.read_id(e[1])
        if k == 'dot':
            return self.read_dot(e)
        if k == 'idx':
            return self.read_idx(e)
        if k == 'dsl':
            return f'F_ds_list_find_value(self,other,Args{{{self.ex(e[1])},{self.ex(e[2])}}})'
        if k == 'call':
            return self.call(e)
        if k == 'bin':
            return self.binop(e)
        if k == 'un':
            a = self.ex(e[2])
            if e[1] == '!':
                return f'Value(!T({a}))'
            if e[1] == '-':
                return f'(-({a}))'
            if e[1] == '~':
                return f'Value((double)(~(long long)Num({a})))'
            return a
        if k in ('pre', 'post'):
            ref, setup = self.lval(e[2])
            op = '+' if e[1] == '++' else '-'
            if k == 'pre':
                return f'([&]()->Value{{ {setup} auto&& __r={ref}; Value __n=(Value)__r {op} Value(1.0); __r=__n; return __n; }})()'
            return f'([&]()->Value{{ {setup} auto&& __r={ref}; Value __o=(Value)__r; __r=__o {op} Value(1.0); return __o; }})()'
        raise ValueError(f'expr {e!r}')

    def binop(self, e):
        op, a, b = e[1], self.ex(e[2]), self.ex(e[3])
        if op == '&&':
            return f'Value(T({a}) && T({b}))'
        if op == '||':
            return f'Value(T({a}) || T({b}))'
        if op == '^^':
            return f'Value(T({a}) != T({b}))'
        if op in ('=', '=='):
            return f'Value(Eq({a},{b}))'
        if op == '!=':
            return f'Value(!Eq({a},{b}))'
        if op in ('<', '<=', '>', '>='):
            return f'Value(Cmp({a},{b}) {op} 0)'
        if op == 'div':
            return f'Div({a},{b})'
        if op == '%':
            return f'Mod({a},{b})'
        if op in ('<<', '>>', '|', '&', '^'):
            return f'Bit{ {"<<":"Shl",">>":"Shr","|":"Or","&":"And","^":"Xor"}[op] }({a},{b})'
        return f'({a} {op} {b})'

    # --- plain identifiers
    def read_id(self, n):
        if n in self.locals:
            return f'L_{n}'
        m = re.fullmatch(r'argument(\d+)', n)
        if m:
            return f'args[{m.group(1)}]'
        if n == 'self':
            return 'Value(-1.0)'
        if n == 'other':
            return 'Value(-2.0)'
        if n == 'global':
            return 'Value(-5.0)'
        if n in ('true', 'false'):
            return f'Value({1.0 if n == "true" else 0.0})'
        if n == 'undefined':
            return 'Value(0.0)'
        if n == 'id':
            return 'Value((double)self->id)'
        if n in CONSTANTS:
            return f'Value({n})'
        if n in self.ctx.globalvars:
            return f'GV({self.ctx.var_id(n)})'
        if n in INST_BUILTINS and not INST_BUILTINS[n]:
            return f'self->get(BI_{n})'
        if n in GLOBAL_BUILTINS and not GLOBAL_BUILTINS[n]:
            return f'GBget(GB_{n})'
        c = self.resolve_const(n)
        if c:
            return f'Value((double){c})'
        if n in self.ctx.scripts:
            return f'Value((double)SCR_{n})'
        self.ctx.unknown_free[n] = self.ctx.unknown_free.get(n, 0) + 1
        return f'self->vget({self.ctx.var_id(n)})'

    # --- instance reference for dot access: returns C++ expr for Inst*
    def inst_of(self, x):
        if x[0] == 'id':
            n = x[1]
            if n == 'self':
                return 'self'
            if n == 'other':
                return 'other'
            if n not in self.locals and n not in INST_BUILTINS:
                if n in self.ctx.sets['OBJ']:
                    return f'FirstInst(OBJ_{n})'
        return f'ToInst({self.ex(x)},self,other)'

    def read_dot(self, e):
        x, n = e[1], e[2]
        if x == ('id', 'global'):
            return self.read_global(n)
        p = self.inst_of(x)
        return self.member_read(p, n)

    def member_read(self, p, n):
        if n in INST_BUILTINS and not INST_BUILTINS[n]:
            return f'{p}->get(BI_{n})'
        if n in INST_BUILTINS:
            return f'{p}->get(BI_{n},0)'
        return f'{p}->vget({self.ctx.var_id(n)})'

    def read_global(self, n):
        if n in GLOBAL_BUILTINS and not GLOBAL_BUILTINS[n]:
            return f'GBget(GB_{n})'
        return f'GV({self.ctx.var_id(n)})'

    def read_idx(self, e):
        base, idx = e[1], e[2]
        i1 = f'(int)Num({self.ex(idx[0])})'
        i2 = f'(int)Num({self.ex(idx[1])})' if len(idx) > 1 else None
        # argument[i]
        if base == ('id', 'argument'):
            return f'args[{i1}]'
        # builtin arrays
        if base[0] == 'id' and base[1] in INST_BUILTINS and INST_BUILTINS[base[1]] and base[1] not in self.locals:
            return f'self->get(BI_{base[1]},{i1})'
        if base[0] == 'id' and base[1] in GLOBAL_BUILTINS and GLOBAL_BUILTINS[base[1]]:
            return f'GBget(GB_{base[1]},{i1})'
        if base[0] == 'dot' and base[1] != ('id', 'global') and base[2] in INST_BUILTINS and INST_BUILTINS[base[2]]:
            return f'{self.inst_of(base[1])}->get(BI_{base[2]},{i1})'
        if base[0] == 'dot' and base[1] == ('id', 'global') and base[2] in GLOBAL_BUILTINS and GLOBAL_BUILTINS[base[2]]:
            return f'GBget(GB_{base[2]},{i1})'
        b = self.ex(base)
        if i2 is not None:
            return f'ArrGet({b},{i1},{i2})'
        return f'ArrGet({b},{i1})'

    # --- function calls
    def call(self, e):
        name, args = e[1], e[2]
        cargs = ','.join(self.ex(a) for a in args)
        if name == 'event_inherited':
            return f'EventInherited(self,other,{self.owner_idx()},{self.ev_type},{self.ev_num})'
        if name in self.ctx.scripts:
            self.ctx.script_calls.add(name)
            return f'S_{name}(self,other,Args{{{cargs}}})'
        self.ctx.builtin_calls[name] = self.ctx.builtin_calls.get(name, 0) + 1
        return f'F_{name}(self,other,Args{{{cargs}}})'

    def owner_idx(self):
        return f'OBJ_{self.owner}' if self.kind == 'event' else '-1'

    # ---------------- lvalues -> (ref_expr, setup_code)
    def lval(self, e):
        k = e[0]
        if k == 'id':
            n = e[1]
            if n in self.locals:
                return f'L_{n}', ''
            m = re.fullmatch(r'argument(\d+)', n)
            if m:
                return f'args.ref({m.group(1)})', ''
            if n in self.ctx.globalvars:
                return f'GV({self.ctx.var_id(n)})', ''
            if n in INST_BUILTINS and not INST_BUILTINS[n]:
                return f'BRef(self,BI_{n})', ''
            if n in GLOBAL_BUILTINS and not GLOBAL_BUILTINS[n]:
                return f'BRef(nullptr,GB_{n})', ''
            self.ctx.unknown_free[n] = self.ctx.unknown_free.get(n, 0) + 1
            return f'self->var({self.ctx.var_id(n)})', ''
        if k == 'dot':
            x, n = e[1], e[2]
            if x == ('id', 'global'):
                if n in GLOBAL_BUILTINS and not GLOBAL_BUILTINS[n]:
                    return f'BRef(nullptr,GB_{n})', ''
                return f'GV({self.ctx.var_id(n)})', ''
            p = self.t()
            setup = f'Inst* {p}={self.inst_of(x)};'
            if n in INST_BUILTINS and not INST_BUILTINS[n]:
                return f'BRef({p},BI_{n})', setup
            return f'{p}->var({self.ctx.var_id(n)})', setup
        if k == 'idx':
            base, idx = e[1], e[2]
            i = self.t()
            setup = f'int {i}=(int)Num({self.ex(idx[0])});'
            j = None
            if len(idx) > 1:
                j = self.t()
                setup += f' int {j}=(int)Num({self.ex(idx[1])});'
            if base == ('id', 'argument'):
                return f'args.ref({i})', setup
            if base[0] == 'id' and base[1] in INST_BUILTINS and INST_BUILTINS[base[1]] and base[1] not in self.locals:
                return f'BRef(self,BI_{base[1]},{i})', setup
            if base[0] == 'id' and base[1] in GLOBAL_BUILTINS and GLOBAL_BUILTINS[base[1]]:
                return f'BRef(nullptr,GB_{base[1]},{i})', setup
            if base[0] == 'dot' and base[1] != ('id', 'global') and base[2] in INST_BUILTINS and INST_BUILTINS[base[2]]:
                p = self.t()
                setup += f' Inst* {p}={self.inst_of(base[1])};'
                return f'BRef({p},BI_{base[2]},{i})', setup
            if base[0] == 'dot' and base[1] == ('id', 'global') and base[2] in GLOBAL_BUILTINS and GLOBAL_BUILTINS[base[2]]:
                return f'BRef(nullptr,GB_{base[2]},{i})', setup
            bref, bsetup = self.lval(base)
            setup = bsetup + ' ' + setup
            if j is not None:
                return f'({bref}).at({i},{j})', setup
            return f'({bref}).at({i})', setup
        raise ValueError(f'lval {e!r}')

    # ---------------- statements
    def st(self, s, ind=''):
        k = s[0]
        if k == 'block':
            return ''.join(self.st(x, ind) for x in s[1])
        if k == 'var' or k == 'gvar':
            return ''
        if k == 'assign':
            return self.assign(s, ind)
        if k == 'expr':
            e = s[1]
            if e[0] in ('pre', 'post'):
                ref, setup = self.lval(e[2])
                op = '+' if e[1] == '++' else '-'
                return f'{ind}{{ {setup} auto&& __r={ref}; __r=(Value)__r {op} Value(1.0); }}\n'
            return f'{ind}(void)({self.ex(e)});\n'
        if k == 'if':
            out = f'{ind}if (T({self.ex(s[1])})) {{\n{self.st(s[2], ind + "  ")}{ind}}}'
            if s[3] is not None:
                out += f' else {{\n{self.st(s[3], ind + "  ")}{ind}}}'
            return out + '\n'
        if k == 'while':
            return f'{ind}while (T({self.ex(s[1])})) {{\n{self.st(s[2], ind + "  ")}{ind}}}\n'
        if k == 'for':
            init = self.stmt_expr(s[1])
            step = self.stmt_expr(s[3])
            return (f'{ind}for ({init}; T({self.ex(s[2])}); {step}) {{\n'
                    f'{self.st(s[4], ind + "  ")}{ind}}}\n')
        if k == 'repeat':
            v = self.t()
            return (f'{ind}for (int {v}=(int)Num({self.ex(s[1])}); {v}>0; --{v}) {{\n'
                    f'{self.st(s[2], ind + "  ")}{ind}}}\n')
        if k == 'do':
            return f'{ind}do {{\n{self.st(s[1], ind + "  ")}{ind}}} while (!T({self.ex(s[2])}));\n'
        if k == 'switch':
            return self.switch(s, ind)
        if k == 'with':
            return self.with_(s, ind)
        if k == 'break':
            return f'{ind}break;\n'
        if k == 'continue':
            return f'{ind}continue;\n'
        if k == 'exit':
            return f'{ind}return{"" if self.kind == "event" else " Value(0.0)"};\n'
        if k == 'return':
            if self.kind == 'event':
                return f'{ind}{{ (void)({self.ex(s[1])}); return; }}\n'
            return f'{ind}return {self.ex(s[1])};\n'
        raise ValueError(f'stmt {s!r}')

    def stmt_expr(self, s):
        if s is None:
            return ''
        if s[0] == 'block':
            s = ('block', [x for x in s[1] if x[0] != 'var'])
        return f'[&]{{\n{self.st(s)}}}()'

    def assign(self, s, ind):
        op, lv, rhs = s[1], s[2], s[3]
        if lv[0] == 'dsl':
            lst, ix, r = self.ex(lv[1]), self.ex(lv[2]), self.ex(rhs)
            if op != '=':
                r = f'((Value)F_ds_list_find_value(self,other,Args{{{lst},{ix}}}) {op[:-1]} {r})'
            return f'{ind}F_ds_list_replace(self,other,Args{{{lst},{ix},{r}}});\n'
        ref, setup = self.lval(lv)
        r = self.ex(rhs)
        if op == '=':
            if setup:
                return f'{ind}{{ Value __v={r}; {setup} {ref}=__v; }}\n'
            return f'{ind}{ref}={r};\n'
        bop = op[:-1]
        mapping = {'+': '+', '-': '-', '*': '*', '/': '/'}
        if bop in mapping:
            comb = f'(Value)__r {bop} __v'
        else:
            fn = {'|': 'BitOr', '&': 'BitAnd', '^': 'BitXor', '<<': 'BitShl', '>>': 'BitShr'}[bop]
            comb = f'{fn}((Value)__r,__v)'
        return f'{ind}{{ Value __v={r}; {setup} auto&& __r={ref}; __r={comb}; }}\n'

    def switch(self, s, ind):
        v = self.t()
        c = self.t()
        cases = s[2]
        out = f'{ind}{{ Value {v}={self.ex(s[1])}; int {c}=-1;\n'
        default_idx = None
        first = True
        for i, (ce, _) in enumerate(cases):
            if ce is None:
                default_idx = i
                continue
            out += f'{ind}  {"if" if first else "else if"} (Eq({v},{self.ex(ce)})) {c}={i};\n'
            first = False
        dflt = default_idx if default_idx is not None else len(cases)
        if first:
            out += f'{ind}  {c}={dflt};\n'
        else:
            out += f'{ind}  else {c}={dflt};\n'
        out += f'{ind}  switch ({c}) {{\n'
        for i, (ce, body) in enumerate(cases):
            out += f'{ind}  case {i}: {{\n'
            out += ''.join(self.st(x, ind + '    ') for x in body)
            out += f'{ind}  }}\n'
        out += f'{ind}  }}\n{ind}}}\n'
        return out

    def with_(self, s, ind):
        w = self.t()
        o = self.t()
        out = (f'{ind}{{ Inst* const {o}=self; Inst* const {o}b=other;\n'
               f'{ind}  for (Inst* {w} : WithList({self.ex(s[1])},{o},{o}b)) {{\n'
               f'{ind}    if (!{w}->alive) continue;\n'
               f'{ind}    Inst* const other={o}; Inst* const self={w}; (void)other; (void)self;\n')
        out += self.st(s[2], ind + '    ')
        out += f'{ind}  }}\n{ind}}}\n'
        return out


def collect_locals(ast, out):
    k = ast[0]
    if k == 'block':
        for x in ast[1]:
            collect_locals(x, out)
    elif k == 'var':
        out.update(ast[1])
    elif k in ('if',):
        collect_locals(ast[2], out)
        if ast[3]:
            collect_locals(ast[3], out)
    elif k in ('while', 'with'):
        collect_locals(ast[2], out)
    elif k == 'for':
        if ast[1]:
            collect_locals(ast[1], out)
        collect_locals(ast[4], out)
    elif k == 'repeat':
        collect_locals(ast[2], out)
    elif k == 'do':
        collect_locals(ast[1], out)
    elif k == 'switch':
        for _, body in ast[2]:
            for x in body:
                collect_locals(x, out)


def collect_globalvars(ast, out):
    k = ast[0]
    if k == 'gvar':
        out.update(ast[1])
    elif k == 'block':
        for x in ast[1]:
            collect_globalvars(x, out)
    elif k in ('if',):
        collect_globalvars(ast[2], out)
        if ast[3]:
            collect_globalvars(ast[3], out)
    elif k in ('while', 'with', 'repeat'):
        collect_globalvars(ast[2], out)
    elif k == 'for':
        collect_globalvars(ast[4], out)
    elif k == 'do':
        collect_globalvars(ast[1], out)
    elif k == 'switch':
        for _, body in ast[2]:
            for x in body:
                collect_globalvars(x, out)


def gen_function(ctx, kind, owner, src, sig_name, ev_type=0, ev_num=0):
    ast = parse(src, sig_name)
    g = FnGen(ctx, kind, owner, ev_type, ev_num)
    lv = set()
    collect_locals(ast, lv)
    g.locals = lv
    body = g.st(ast, '  ')
    decl = ''.join(f'  Value L_{n};\n' for n in sorted(lv))
    if kind == 'event':
        sig = f'void {sig_name}(Inst* self, Inst* other)'
        tail = ''
    else:
        sig = f'Value {sig_name}(Inst* self, Inst* other, Args args)'
        tail = '  return Value(0.0);\n'
    return f'{sig} {{\n  (void)other;\n{decl}{body}{tail}}}\n'
