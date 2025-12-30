// Copyright (c) Len Popp
// This source code is licensed under the MIT license - see LICENSE file.

#pragma once
#include <chrono>
#include <concepts>
#include <print>

void showTime(std::invocable auto func)
{
    using clock = std::chrono::steady_clock;
    clock::time_point tStart = clock::now();
    func();
    clock::duration dt = clock::now() - tStart;
    double time = (double)dt.count() * (double)clock::period::num / (double)clock::period::den;
    std::println("Time: {:.02f} seconds", time);
}
