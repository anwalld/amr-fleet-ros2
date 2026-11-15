#pragma once

#include <cctype>
#include <string>

namespace robot_tasks {

inline std::string SanitizeId(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  for (const char ch : value) {
    const auto uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) || ch == '_' || ch == '-') {
      result.push_back(ch);
    } else {
      result.push_back('_');
    }
  }
  return result;
}

}  // namespace robot_tasks
