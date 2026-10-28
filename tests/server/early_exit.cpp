// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
#include <iostream>

int main() {
    std::cerr << "Controlled startup child exited before readiness\n";
    return 66;
}
