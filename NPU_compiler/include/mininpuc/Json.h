#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mininpu {

// 最小 JSON 解析器：只支持本项目的 spec/meta 文件（对象、数组、字符串、数字、bool、null）。
// 解析失败抛 std::runtime_error，带位置信息。
struct JsonValue {
  enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<JsonValue> arr;
  std::map<std::string, JsonValue> obj;

  const JsonValue& at(const std::string& k) const;   // 缺 key 抛错
  const JsonValue* find(const std::string& k) const; // 缺 key 返回 nullptr
  double asNum() const;
  int64_t asInt() const { return static_cast<int64_t>(asNum()); }
  const std::string& asStr() const;
};

JsonValue parseJson(const std::string& text);
JsonValue readJsonFile(const std::string& path);

} // namespace mininpu
