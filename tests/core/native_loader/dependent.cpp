// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#if defined(_WIN32)
extern "C" __declspec(dllimport) int ark_loader_sibling_value();
#define LOADER_EXPORT extern "C" __declspec(dllexport)
#else
extern "C" int ark_loader_sibling_value();
#define LOADER_EXPORT extern "C" __attribute__((visibility("default")))
#endif

LOADER_EXPORT int ark_loader_dependency_probe() {
    return ark_loader_sibling_value() + 1;
}
