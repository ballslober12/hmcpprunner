#include "rt.h"
#include <algorithm>

Value::Value(const Value& o) : t(o.t), d(o.d), s(o.s) {
  if (o.a) a = std::make_shared<Arr>(*o.a);
}
Value& Value::operator=(const Value& o) {
  if (this == &o) return *this;
  t = o.t; d = o.d; s = o.s;
  if (o.a) a = std::make_shared<Arr>(*o.a); else a.reset();
  return *this;
}
Value& Value::at(int i) {
  if (t != A || !a) { t = A; a = std::make_shared<Arr>(); d = 0; s.clear(); }
  if (i < 0) i = 0;
  if ((size_t)i >= a->v.size()) a->v.resize(i + 1);
  return a->v[i];
}
Value& Value::at(int i, int j) {
  if (t != A || !a) { t = A; a = std::make_shared<Arr>(); d = 0; s.clear(); }
  a->two = true;
  return a->m2[(long long)i * 100000 + j];
}
Value ArrGet(const Value& v, int i) {
  if (v.t != Value::A || !v.a) return Value(0.0);
  if (i < 0 || (size_t)i >= v.a->v.size()) return Value(0.0);
  return v.a->v[i];
}
Value ArrGet(const Value& v, int i, int j) {
  if (v.t != Value::A || !v.a) return Value(0.0);
  auto it = v.a->m2.find((long long)i * 100000 + j);
  if (it == v.a->m2.end()) return Value(0.0);
  return it->second;
}

double Num(const Value& v) {
  if (v.t == Value::R) return v.d;
  if (v.t == Value::S) return atof(v.s.c_str());
  return 0;
}
std::string Str(const Value& v) {
  if (v.t == Value::S) return v.s;
  if (v.t == Value::A) return "";
  char buf[64];
  double d = v.d;
  if (d == std::floor(d) && std::fabs(d) < 1e15) { snprintf(buf, sizeof buf, "%lld", (long long)d); return buf; }
  snprintf(buf, sizeof buf, "%.2f", d);
  std::string r = buf;
  while (!r.empty() && r.back() == '0') r.pop_back();
  if (!r.empty() && r.back() == '.') r.pop_back();
  return r;
}
bool Eq(const Value& a, const Value& b) {
  if (a.t == Value::S || b.t == Value::S) return a.t == b.t && a.s == b.s;
  return std::fabs(a.d - b.d) < 1e-5;
}
int Cmp(const Value& a, const Value& b) {
  if (a.t == Value::S && b.t == Value::S) { int c = a.s.compare(b.s); return c < 0 ? -1 : c > 0 ? 1 : 0; }
  double x = Num(a), y = Num(b);
  if (std::fabs(x - y) < 1e-9) return 0;
  return x < y ? -1 : 1;
}
Value operator+(const Value& a, const Value& b) {
  if (a.t == Value::S || b.t == Value::S) return Value(Str(a) + Str(b));
  return Value(a.d + b.d);
}
Value operator-(const Value& a, const Value& b) { return Value(Num(a) - Num(b)); }
Value operator*(const Value& a, const Value& b) { return Value(Num(a) * Num(b)); }
Value operator/(const Value& a, const Value& b) {
  double y = Num(b);
  return Value(y == 0 ? 0.0 : Num(a) / y);
}
Value operator-(const Value& a) { return Value(-Num(a)); }
Value Div(const Value& a, const Value& b) {
  double y = Num(b);
  return Value(y == 0 ? 0.0 : std::trunc(Num(a) / y));
}
Value Mod(const Value& a, const Value& b) {
  double y = Num(b);
  return Value(y == 0 ? 0.0 : std::fmod(Num(a), y));
}
static long long L(const Value& v) { return (long long)Num(v); }
Value BitAnd(const Value& a, const Value& b) { return Value((double)(L(a) & L(b))); }
Value BitOr(const Value& a, const Value& b) { return Value((double)(L(a) | L(b))); }
Value BitXor(const Value& a, const Value& b) { return Value((double)(L(a) ^ L(b))); }
Value BitShl(const Value& a, const Value& b) { return Value((double)(L(a) << L(b))); }
Value BitShr(const Value& a, const Value& b) { return Value((double)(L(a) >> L(b))); }

const Value& Args::operator[](int i) const {
  static Value zero;
  if (i < 0 || i >= n) { zero = Value(0.0); return zero; }
  return p[i];
}

// ------------------------------------------------------------------ Inst variables
Value& Inst::var(int id) {
  auto it = std::lower_bound(vars.begin(), vars.end(), id,
                             [](const std::pair<int, Value>& p, int k) { return p.first < k; });
  if (it != vars.end() && it->first == id) return it->second;
  it = vars.insert(it, {id, Value()});
  return it->second;
}
const Value& Inst::vget(int id) const {
  static const Value zero;
  auto it = std::lower_bound(vars.begin(), vars.end(), id,
                             [](const std::pair<int, Value>& p, int k) { return p.first < k; });
  if (it != vars.end() && it->first == id) return it->second;
  return zero;
}
bool Inst::hasvar(int id) const {
  auto it = std::lower_bound(vars.begin(), vars.end(), id,
                             [](const std::pair<int, Value>& p, int k) { return p.first < k; });
  return it != vars.end() && it->first == id;
}
