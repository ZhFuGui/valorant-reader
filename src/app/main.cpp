#include "app/application.hpp"
#include "memory/read_error.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>
#include <vector>
#include <windows.h>

// 标准宽字符 CRT 已完成静态初始化和命令行拆分；GUI 入口不分配控制台、不创建窗口。
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        const auto argc = __argc;
        const auto argv = __wargv;
        std::vector<std::wstring_view> arguments;
        arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
        for (int i = 1; i < argc; ++i)
            arguments.emplace_back(argv[i]);
        return valorant::app::run(arguments, std::cout, std::cerr);
    } catch (const valorant::memory::ReadUnavailable& error) {
        std::cerr << "Observation unavailable: " << error.what() << '\n';
        return static_cast<int>(valorant::app::ExitCode::ObservationUnavailable);
    } catch (const std::exception& error) {
        std::cerr << "Initialization failed: " << error.what() << '\n';
        return static_cast<int>(valorant::app::ExitCode::RuntimeFailure);
    }
}
