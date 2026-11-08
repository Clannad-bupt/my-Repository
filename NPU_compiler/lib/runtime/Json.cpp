#include "mininpuc/Json.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mininpu {

const JsonValue& JsonValue::at(const std::string& k) const {
  if (kind != Obj) throw std::runtime_error("json: not an object, key=" + k);
  auto it = obj.find(k);
  if (it == obj.end()) throw std::runtime_error("json: missing key '" + k + "'");
  return it->second;
}

const JsonValue* JsonValue::find(const std::string& k) const {
  if (kind != Obj) return nullptr;
  auto it = obj.find(k);
  return it == obj.end() ? nullptr : &it->second;
}

double JsonValue::asNum() const {
  if (kind != Num) throw std::runtime_error("json: not a number");
  return num;
}

const std::string& JsonValue::asStr() const {
  if (kind != Str) throw std::runtime_error("json: not a string");
  return str;
}

namespace {
struct Parser {
  const std::string& s;
  size_t i = 0;
  explicit Parser(const std::string& s) : s(s) {}

  [[noreturn]] void fail(const std::string& msg) {
    throw std::runtime_error("json parse error at byte " + std::to_string(i) + ": " + msg);
  }
  void ws() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
  char peek() { ws(); if (i >= s.size()) fail("unexpected end"); return s[i]; }
  bool eat(char c) { ws(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
  void expect(char c) { if (!eat(c)) fail(std::string("expected '") + c + "'"); }

  JsonValue value() {
    char c = peek();
    if (c == '{') return object();
    if (c == '[') return array();
    if (c == '"') { JsonValue v; v.kind = JsonValue::Str; v.str = string(); return v; }
    if (c == 't') { lit("true"); JsonValue v; v.kind = JsonValue::Bool; v.b = true; return v; }
    if (c == 'f') { lit("false"); JsonValue v; v.kind = JsonValue::Bool; return v; }
    if (c == 'n') { lit("null"); return {}; }
    return number();
  }
  void lit(const char* w) {
    for (const char* p = w; *p; ++p)
      if (i >= s.size() || s[i++] != *p) fail("bad literal");
  }
  JsonValue object() {
    JsonValue v; v.kind = JsonValue::Obj;
    expect('{');
    if (eat('}')) return v;
    while (true) {
      if (peek() != '"') fail("expected object key");
      std::string k = string();
      expect(':');
      v.obj[std::move(k)] = value();
      if (eat('}')) return v;
      expect(',');
    }
  }
  JsonValue array() {
    JsonValue v; v.kind = JsonValue::Arr;
    expect('[');
    if (eat(']')) return v;
    while (true) {
      v.arr.push_back(value());
      if (eat(']')) return v;
      expect(',');
    }
  }
  std::string string() {
    expect('"');
    std::string out;
    while (i < s.size() && s[i] != '"') {
      if (s[i] == '\\') {
        ++i; if (i >= s.size()) fail("bad escape");
        switch (s[i]) { case 'n': out += '\n'; break; case 't': out += '\t'; break;
                        case 'r': out += '\r'; break; default: out += s[i]; }
      } else out += s[i];
      ++i;
    }
    if (i >= s.size()) fail("unterminated string");
    ++i;
    return out;
  }
  JsonValue number() {
    ws();
    size_t start = i;
    if (i < s.size() && (s[i] == '-')) ++i;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' ||
                            s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) ++i;
    if (start == i) fail("expected number");
    JsonValue v; v.kind = JsonValue::Num;
    v.num = std::stod(s.substr(start, i - start));
    return v;
  }
};
} // namespace

JsonValue parseJson(const std::string& text) {
  Parser p(text);
  JsonValue v = p.value();
  p.ws();
  if (p.i != text.size()) p.fail("trailing content");
  return v;
}

JsonValue readJsonFile(const std::string& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot open " + path);
  std::stringstream ss;
  ss << f.rdbuf();
  return parseJson(ss.str());
}

} // namespace mininpu
