#pragma once

#include <sstream>
#include <string>
#include <vector>

namespace csvutil {

inline std::vector<std::string> SplitCsv(const std::string& line) {
  std::vector<std::string> out;
  std::stringstream ss(line);
  std::string token;
  while (std::getline(ss, token, ',')) {
    out.push_back(token);
  }
  return out;
}

inline double SafeParseDouble(const std::vector<std::string>& cols,
                              size_t idx,
                              double fallback) {
  if (idx >= cols.size()) {
    return fallback;
  }
  try {
    return std::stod(cols[idx]);
  } catch (...) {
    return fallback;
  }
}

inline bool TryParseDouble(const std::vector<std::string>& cols,
                           size_t idx,
                           double& out) {
  if (idx >= cols.size()) {
    return false;
  }
  try {
    out = std::stod(cols[idx]);
    return true;
  } catch (...) {
    return false;
  }
}

inline int SafeParseInt(const std::vector<std::string>& cols,
                        size_t idx,
                        int fallback) {
  if (idx >= cols.size()) {
    return fallback;
  }
  try {
    return std::stoi(cols[idx]);
  } catch (...) {
    return fallback;
  }
}

inline bool TryParseInt(const std::vector<std::string>& cols,
                        size_t idx,
                        int& out) {
  if (idx >= cols.size()) {
    return false;
  }
  try {
    out = std::stoi(cols[idx]);
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace csvutil
