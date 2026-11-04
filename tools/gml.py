"""GML (GameMaker 8.x/Studio 1.4 dialect) lexer + parser -> AST.

AST nodes are tuples:
  expressions: ('num',v) ('str',s) ('id',name) ('dot',expr,name) ('idx',expr,[exprs])
               ('call',name,[args]) ('bin',op,a,b) ('un',op,a) ('pre',op,lv) ('post',op,lv)
               ('assign',op,lv,expr)   (only produced for statement-level / for-header assigns)
  statements:  ('block',[st]) ('var',[names]) ('gvar',[names]) ('if',c,t,e) ('while',c,b)
               ('for',init,cond,step,b) ('repeat',n,b) ('do',b,c) ('switch',e,[(case_expr|None,[st])])
               ('with',e,b) ('break',) ('continue',) ('exit',) ('return',e) ('expr',e) ('assign',...)
"""
import re

KEYWORDS = {'if', 'else', 'while', 'for', 'repeat', 'do', 'until', 'switch', 'case', 'default', 'with',
            'break', 'continue', 'exit', 'return', 'var', 'globalvar', 'begin', 'end'}
WORD_OPS = {'and': '&&', 'or': '||', 'xor': '^^', 'not': '!', 'mod': '%', 'div': 'div'}

TOKEN_RE = re.compile(r'''
    (?P<ws>\s+)
  | (?P<lc>//[^\n]*)
  | (?P<bc>/\*.*?(?:\*/|\Z))
  | (?P<hex>\$[0-9A-Fa-f]+)
  | (?P<num>(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)
  | (?P<str>"[^"]*"|'[^']*')
  | (?P<id>[A-Za-z_][A-Za-z_0-9]*)
  | (?P<op><<=|>>=|\+\+|--|\+=|-=|\*=|/=|\|=|&=|\^=|:=|==|!=|<>|<=|>=|&&|\|\||\^\^|<<|>>|[-+*/%=<>!~&|^()\[\]{},;.:?#@])
''', re.X | re.S)


class GMLError(Exception):
    pass


def lex(src):
    toks = []
    pos = 0
    n = len(src)
    while pos < n:
        m = TOKEN_RE.match(src, pos)
        if not m:
            # Unknown char (e.g. stray unicode): skip it.
            pos += 1
            continue
        pos = m.end()
        k = m.lastgroup
        if k in ('ws', 'lc', 'bc'):
            continue
        t = m.group(k)
        if k == 'hex':
            toks.append(('num', float(int(t[1:], 16))))
        elif k == 'num':
            toks.append(('num', float(t)))
        elif k == 'str':
            toks.append(('str', t[1:-1]))
        elif k == 'id':
            if t in WORD_OPS:
                toks.append(('op', WORD_OPS[t]))
            elif t in KEYWORDS:
                toks.append(('kw', t))
            else:
                toks.append(('id', t))
        else:
            toks.append(('op', '!=' if t == '<>' else ('=' if t == ':=' else t)))
    toks.append(('eof', None))
    return toks


class Parser:
    def __init__(self, src, name='?'):
        self.t = lex(src)
        self.i = 0
        self.name = name

    # --- helpers
    def peek(self, o=0):
        return self.t[min(self.i + o, len(self.t) - 1)]

    def next(self):
        tk = self.t[self.i]
        self.i += 1
        return tk

    def isop(self, v, o=0):
        tk = self.peek(o)
        return tk[0] == 'op' and tk[1] == v

    def iskw(self, v, o=0):
        tk = self.peek(o)
        return tk[0] == 'kw' and tk[1] == v

    def acceptop(self, v):
        if self.isop(v):
            self.i += 1
            return True
        return False

    def expectop(self, v):
        if not self.acceptop(v):
            raise GMLError(f'{self.name}: expected {v!r} got {self.peek()!r} at token {self.i}')

    # --- program
    def parse_program(self):
        out = []
        while self.peek()[0] != 'eof':
            st = self.statement()
            if st is not None:
                out.append(st)
        return ('block', out)

    def block_body(self, endkw):
        out = []
        while True:
            if endkw == '}' and self.isop('}'):
                self.i += 1
                break
            if endkw == 'end' and self.iskw('end'):
                self.i += 1
                break
            if self.peek()[0] == 'eof':
                break  # tolerate unbalanced
            st = self.statement()
            if st is not None:
                out.append(st)
        return ('block', out)

    def statement(self):
        tk = self.peek()
        if tk == ('op', ';'):
            self.i += 1
            return None
        if tk == ('op', '{'):
            self.i += 1
            return self.block_body('}')
        if tk[0] == 'kw':
            k = tk[1]
            if k == 'begin':
                self.i += 1
                return self.block_body('end')
            if k == 'end':  # stray
                self.i += 1
                return None
            if k in ('var', 'globalvar'):
                self.i += 1
                names = []
                inits = []
                while self.peek()[0] == 'id':
                    nm = self.next()[1]
                    names.append(nm)
                    if self.isop('='):
                        self.i += 1
                        inits.append(('assign', '=', ('id', nm), self.expr()))
                    if not self.acceptop(','):
                        break
                self.acceptop(';')
                node = ('var' if k == 'var' else 'gvar', names)
                if inits:
                    return ('block', [node] + inits)
                return node
            if k == 'if':
                self.i += 1
                c = self.expr()
                self.skip_then()
                t = self.statement_nn()
                e = None
                if self.iskw('else'):
                    self.i += 1
                    e = self.statement_nn()
                return ('if', c, t, e)
            if k == 'while':
                self.i += 1
                c = self.expr()
                self.skip_do()
                return ('while', c, self.statement_nn())
            if k == 'for':
                self.i += 1
                self.expectop('(')
                if self.iskw('var'):
                    init = self.statement_var_nosemi()
                else:
                    init = None if self.isop(';') else self.simple_statement()
                self.expectop(';')
                cond = ('num', 1.0) if self.isop(';') else self.expr()
                self.expectop(';')
                step = None if self.isop(')') else self.simple_statement()
                self.expectop(')')
                return ('for', init, cond, step, self.statement_nn())
            if k == 'repeat':
                self.i += 1
                n = self.expr()
                return ('repeat', n, self.statement_nn())
            if k == 'do':
                self.i += 1
                b = self.statement_nn()
                if not self.iskw('until'):
                    raise GMLError(f'{self.name}: expected until')
                self.i += 1
                c = self.expr()
                self.acceptop(';')
                return ('do', b, c)
            if k == 'switch':
                self.i += 1
                e = self.expr()
                self.expectop('{')
                cases = []
                cur = None
                while not self.isop('}') and self.peek()[0] != 'eof':
                    if self.iskw('case'):
                        self.i += 1
                        ce = self.expr()
                        self.expectop(':')
                        cur = (ce, [])
                        cases.append(cur)
                    elif self.iskw('default'):
                        self.i += 1
                        self.expectop(':')
                        cur = (None, [])
                        cases.append(cur)
                    else:
                        st = self.statement()
                        if st is not None and cur is not None:
                            cur[1].append(st)
                self.expectop('}')
                return ('switch', e, cases)
            if k == 'with':
                self.i += 1
                e = self.expr()
                self.skip_do()
                return ('with', e, self.statement_nn())
            if k == 'break':
                self.i += 1
                self.acceptop(';')
                return ('break',)
            if k == 'continue':
                self.i += 1
                self.acceptop(';')
                return ('continue',)
            if k == 'exit':
                self.i += 1
                self.acceptop(';')
                return ('exit',)
            if k == 'return':
                self.i += 1
                e = self.expr()
                self.acceptop(';')
                return ('return', e)
            if k in ('else', 'until', 'case', 'default'):
                # stray keyword - skip to avoid infinite loop
                self.i += 1
                return None
        st = self.simple_statement()
        self.acceptop(';')
        return st

    def statement_var_nosemi(self):
        self.i += 1
        names, inits = [], []
        while self.peek()[0] == 'id':
            nm = self.next()[1]
            names.append(nm)
            if self.isop('='):
                self.i += 1
                inits.append(('assign', '=', ('id', nm), self.expr()))
            if not self.acceptop(','):
                break
        return ('block', [('var', names)] + inits)

    def statement_nn(self):
        st = self.statement()
        return st if st is not None else ('block', [])

    def skip_then(self):
        if self.peek()[0] == 'id' and self.peek()[1] == 'then':
            self.i += 1

    def skip_do(self):
        if self.peek()[0] == 'id' and self.peek()[1] == 'do':
            self.i += 1

    ASSIGN_OPS = {'=', '+=', '-=', '*=', '/=', '|=', '&=', '^=', '<<=', '>>='}

    def simple_statement(self):
        """assignment / ++ / -- / call expression"""
        if self.isop('++') or self.isop('--'):
            op = self.next()[1]
            lv = self.postfix()
            return ('expr', ('pre', op, lv))
        e = self.postfix()
        tk = self.peek()
        if e[0] == 'post':
            return ('expr', e)
        if tk[0] == 'op' and tk[1] in self.ASSIGN_OPS and e[0] in ('id', 'dot', 'idx', 'dsl'):
            self.i += 1
            rhs = self.expr()
            return ('assign', tk[1], e, rhs)
        if tk[0] == 'op' and tk[1] in ('++', '--') and e[0] in ('id', 'dot', 'idx'):
            self.i += 1
            return ('expr', ('post', tk[1], e))
        # expression statement; may continue as an operator expression (rare)
        e = self.binary_rest(e, 0)
        return ('expr', e)

    # --- expressions
    # GML precedence: logical (&& || ^^) < comparison < add/bitwise/shift < mul/div/mod < unary
    LEVELS = [
        {'&&', '||', '^^'},
        {'<', '<=', '==', '=', '!=', '>', '>='},
        {'+', '-', '<<', '>>', '|', '&', '^'},
        {'*', '/', '%', 'div'},
    ]

    def expr(self):
        return self.binary(0)

    def binary(self, lvl):
        if lvl >= len(self.LEVELS):
            return self.unary()
        left = self.binary(lvl + 1)
        return self.binary_rest(left, lvl)

    def binary_rest(self, left, lvl):
        # continue parsing binary operators at or above `lvl` given an already-parsed operand
        # (only exact for lvl==0 starting from postfix; good enough for rare statement cases)
        for L in range(len(self.LEVELS) - 1, lvl - 1, -1):
            pass
        return self._climb(left, lvl)

    def _climb(self, left, lvl):
        # operand `left` was parsed at the unary level; finish all levels from tightest to loosest
        for L in range(len(self.LEVELS) - 1, lvl - 1, -1):
            left = self._level_rest(left, L)
        return left

    def _level_rest(self, left, L):
        ops = self.LEVELS[L]
        while True:
            tk = self.peek()
            if tk[0] == 'op' and tk[1] in ops:
                # `=` at the top of a statement-ish context is still comparison here
                self.i += 1
                right = self.binary(L + 1)
                left = ('bin', tk[1], left, right)
            else:
                break
        return left

    def unary(self):
        tk = self.peek()
        if tk[0] == 'op':
            if tk[1] in ('!', '-', '+', '~'):
                self.i += 1
                return ('un', tk[1], self.unary())
            if tk[1] in ('++', '--'):
                self.i += 1
                return ('pre', tk[1], self.unary())
        return self.postfix()

    def postfix(self):
        tk = self.next()
        if tk[0] == 'num':
            e = ('num', tk[1])
        elif tk[0] == 'str':
            e = ('str', tk[1])
        elif tk[0] == 'op' and tk[1] == '(':
            e = self.expr()
            self.expectop(')')
        elif tk[0] == 'id':
            if self.isop('('):
                self.i += 1
                args = []
                while not self.isop(')'):
                    args.append(self.expr())
                    if not self.acceptop(','):
                        break
                self.expectop(')')
                e = ('call', tk[1], args)
            else:
                e = ('id', tk[1])
        elif tk[0] == 'op' and tk[1] == '-':
            return ('un', '-', self.postfix())
        elif tk[0] == 'op' and tk[1] == '@':
            return self.postfix()
        else:
            raise GMLError(f'{self.name}: unexpected token {tk!r} at {self.i}')
        while True:
            if self.isop('.') and self.peek(1)[0] == 'id':
                self.i += 1
                e = ('dot', e, self.next()[1])
            elif self.isop('[') and self.isop('|', 1):
                self.i += 2
                idx = self.expr()
                self.expectop(']')
                e = ('dsl', e, idx)
            elif self.isop('['):
                self.i += 1
                idx = [self.expr()]
                while self.acceptop(','):
                    idx.append(self.expr())
                self.expectop(']')
                e = ('idx', e, idx)
            elif (self.isop('++') or self.isop('--')) and e[0] in ('id', 'dot', 'idx'):
                e = ('post', self.next()[1], e)
            else:
                break
        return e


def parse(src, name='?'):
    return Parser(src, name).parse_program()
