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

//#include "ark/misc.hpp"
#include "ark/platform.hpp"

namespace ark {

class vector3d {
public:
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    vector3d() = default;

    vector3d(float f3[]) {
        x = f3[0];
        y = f3[1];
        z = f3[2];
    }

    vector3d(float a, float b, float c)
        : x(a)
        , y(b)
        , z(c) {
    }

    vector3d(const vector3d& rhs) {
        x = rhs.x;
        y = rhs.y;
        z = rhs.z;
    }

    const vector3d& operator=(const vector3d& rhs) {
        if (this != &rhs) {
            x = rhs.x;
            y = rhs.y;
            z = rhs.z;
        }
        return *this;
    }

    inline bool operator==(const vector3d& rhs) const {
        return IS_FLOAT_ZERO(float, distance(*this, rhs));
    }

    inline bool operator!=(const vector3d& rhs) const {
        return !(*this == rhs);
    }

    float get_length() const {
        return sqrt((x * x) + (y * y) + (z * z));
    }

    bool is_zero() const {
        return (IS_FLOAT_ZERO(float, x) && IS_FLOAT_ZERO(float, y) && IS_FLOAT_ZERO(float, z));
    }

    //std::string ToString() const
    //{
    //    //(x,y,z)
    //    return ARK_FORMAT("({:.2f},{:.2f},{:.2f})", x, y, z);
    //}

    //bool FromString(const std::string& value)
    //{
    //    // TODO:split by new format
    //    // x,y,z
    //    size_t len = value.length();
    //    size_t pos = value.find(',');

    //    if (pos == std::string::npos)
    //    {
    //        return false;
    //    }

    //    std::string str_x = value.substr(0, pos);

    //    const std::string new_value = value.substr(pos + 1, len - pos);
    //    pos = new_value.find(',');

    //    if (pos == std::string::npos)
    //    {
    //        return false;
    //    }

    //    std::string str_y = new_value.substr(0, pos);

    //    std::string str_z;

    //    if ((pos + 1) < new_value.length())
    //    {
    //        str_z = new_value.substr(pos + 1, new_value.length() - pos);
    //    }

    //    try
    //    {
    //        x = ARK_LEXICAL_CAST<float>(str_x);
    //        y = ARK_LEXICAL_CAST<float>(str_y);
    //        z = ARK_LEXICAL_CAST<float>(str_z);
    //        return true;
    //    }
    //    catch (std::system_error& ex)
    //    {
    //        ARK_LOG_ERROR("Vector3 FromString failed, error={}, msg={}", ex.code().message(), ex.what());
    //        ARK_ASSERT_NO_EFFECT(0);
    //        return false;
    //    }

    //    return true;
    //}

    static float distance(const vector3d& a, const vector3d& b) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        return sqrt(dx * dx + dy * dy + dz * dz);
    }
};
} // namespace ark
