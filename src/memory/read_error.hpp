#pragma once
#include <stdexcept>

namespace valorant::memory {
// 会话来源暂不可读或观察发生变化；不把配置、版本或程序错误伪装成字段缺失。
class ReadUnavailable : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
} // namespace valorant::memory
