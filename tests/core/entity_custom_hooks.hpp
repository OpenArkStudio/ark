// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

namespace entity_custom_test {

void record_delete_hook(const void* value) noexcept;

template<typename value_type>
void delete_value(value_type*& value) {
    if (value != nullptr)
    {
        record_delete_hook(value);
        delete value;
        value = nullptr;
    }
}

} // namespace entity_custom_test

// Forced into every TU of the isolated executable before project headers.
#define ARK_DELETE(pointer) entity_custom_test::delete_value(pointer)

// A supported custom allocator can throw while constructing a string value.
#define ARK_NEW_ARRAY_RET(element_type, size)                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        return new element_type[(size)];                                                                               \
    } while (false)
