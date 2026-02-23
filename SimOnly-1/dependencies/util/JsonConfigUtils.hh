#pragma once

#include <string>

namespace configutil {

template <typename Json, typename T>
inline T GetOr(const Json& json_obj, const std::string& key, const T& fallback) {
  if (!json_obj.contains(key)) {
    return fallback;
  }
  return json_obj.at(key).template get<T>();
}

}  // namespace configutil

