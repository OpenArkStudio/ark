/*
 * This source file is part of ARK
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 ArkNX authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"),
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#pragma once

#include "ark/macros.hpp"
#include "ark/noncopyable.hpp"
#include "ark/platform.hpp"

namespace ark {

template<typename element_type>
class singleton : public noncopyable
{
public:
    static element_type* instance() {
        // The C++ initialization guard serializes first access and retries if init throws.
        static const bool initialized = (init(), true);
        (void)initialized;
        return instance_;
    }

    static element_type& get_instance() {
        return *instance();
    }

    static void shutdown() {
        stopped_ = true;
        instance_ = nullptr;
        auto* owned = owned_instance_;
        owned_instance_ = nullptr;
        ARK_DELETE(owned);
    }

    // Borrow single without transferring ownership. Retain an internally created
    // instance until shutdown so temporary injection can restore its pointer.
    static void set_instance(element_type* single) {
        instance_ = single;
    }

private:
    static void init() {
        if (!stopped_ && instance_ == nullptr)
        {
            owned_instance_ = ARK_NEW element_type();
            instance_ = owned_instance_;
        }
    }

    static element_type* instance_;
    static element_type* owned_instance_;
    static bool stopped_;
};

template<typename element_type>
element_type* singleton<element_type>::instance_ = nullptr;

template<typename element_type>
element_type* singleton<element_type>::owned_instance_ = nullptr;

template<typename element_type>
bool singleton<element_type>::stopped_ = false;

} // namespace ark
