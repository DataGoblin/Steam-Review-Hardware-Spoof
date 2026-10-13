#pragma once

#include <string>
#include <string_view>

namespace gpucfg::log {

bool init();
void shutdown();

void write(std::string_view line);

const std::wstring& log_path();

}  // namespace gpucfg::log

#define GCLOG(fmt, ...)                                                \
    do {                                                                \
        char _buf[1024];                                                \
        int _n = _snprintf_s(_buf, sizeof(_buf), _TRUNCATE,             \
                             fmt, __VA_ARGS__);                         \
        if (_n > 0) ::gpucfg::log::write(std::string_view(_buf, _n)); \
    } while (0)
