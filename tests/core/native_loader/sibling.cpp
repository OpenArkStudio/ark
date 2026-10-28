// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#if defined(_WIN32)
#define LOADER_EXPORT extern "C" __declspec(dllexport)
#else
#define LOADER_EXPORT extern "C" __attribute__((visibility("default")))
#endif

LOADER_EXPORT int ark_loader_sibling_value() {
    return 41;
}
