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

// based from https://github.com/atframework/atframe_utils

#include "ark/platform.hpp"

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

namespace ark {

/**
 * @brief just for
 *          xoshiro256**
 *          xoshiro256+
 *          xoroshiro128**
 *          xoroshiro128+
 * @note not support for xoroshiro64** xoroshiro64* xoroshiro512** xoroshiro512* xoroshiro1024** xoroshiro1024*
 */
template<class uint_type, bool is_plus, int iidx, int n1, int n2>
class xoshinro_engine {
public:
    typedef uint_type result_type;
    typedef result_type seed_type[4];

private:
    seed_type xoshinro_seed_;

    static inline result_type rotl(const result_type x, int k) {
        return (x << k) | (x >> ((sizeof(result_type) * 8) - static_cast<result_type>(k)));
    }

    result_type next_init() {
        if constexpr (is_plus) {
            return xoshinro_seed_[0] + xoshinro_seed_[3];
        } else {
            return rotl(xoshinro_seed_[iidx] * 5, 7) * 9;
        }
    }

protected:
    result_type next() {
        const result_type ret = next_init();
        const result_type t = xoshinro_seed_[1] << n1;

        xoshinro_seed_[2] ^= xoshinro_seed_[0];
        xoshinro_seed_[3] ^= xoshinro_seed_[1];
        xoshinro_seed_[1] ^= xoshinro_seed_[2];
        xoshinro_seed_[0] ^= xoshinro_seed_[3];

        xoshinro_seed_[2] ^= t;
        xoshinro_seed_[3] = rotl(xoshinro_seed_[3], n2);

        return ret;
    }

    void jump(const seed_type& jump) {
        result_type s0 = 0;
        result_type s1 = 0;
        result_type s2 = 0;
        result_type s3 = 0;
        for (size_t i = 0; i < sizeof(jump) / sizeof(jump[0]); i++) {
            for (size_t b = 0; b < sizeof(result_type) * 8; b++) {
                if (jump[i] & result_type(1) << b) {
                    s0 ^= xoshinro_seed_[0];
                    s1 ^= xoshinro_seed_[1];
                    s2 ^= xoshinro_seed_[2];
                    s3 ^= xoshinro_seed_[3];
                }
                next();
            }
        }

        xoshinro_seed_[0] = s0;
        xoshinro_seed_[1] = s1;
        xoshinro_seed_[2] = s2;
        xoshinro_seed_[3] = s3;
    }

public:
    xoshinro_engine() {
        xoshinro_seed_[0] = 0;
        xoshinro_seed_[1] = 0;
        xoshinro_seed_[2] = 0;
        xoshinro_seed_[3] = 0;
    }

    xoshinro_engine(result_type s) {
        xoshinro_seed_[0] = 0;
        xoshinro_seed_[1] = 0;
        xoshinro_seed_[2] = 0;
        xoshinro_seed_[3] = 0;
        init_seed(s);
    }

    void init_seed(result_type s) {
        xoshinro_seed_[0] = s;
        xoshinro_seed_[1] = 0xff;
        xoshinro_seed_[2] = 0;
        xoshinro_seed_[3] = 0;

        // just like in Lua 5.4
        for (int i = 0; i < 16; ++i) {
            next();
        }
    }

    template<class it>
    void init_seed(it& first, it last) {
        it begin = first;
        for (int i = 0; i < 4; ++i) {
            if (begin != last) {
                xoshinro_seed_[i] = *begin;
                ++begin;
            } else {
                xoshinro_seed_[i] = 0;
            }
        }

        // just like in Lua 5.4
        for (int i = 0; i < 16; ++i) {
            next();
        }
    }

    result_type random() {
        return next();
    }

    result_type operator()() {
        return random();
    }

    inline const seed_type& get_seed() const {
        return xoshinro_seed_;
    }
};

template<bool is_plus>
class xoshinro_engine_128 : public xoshinro_engine<uint32_t, is_plus, 0, 9, 11> {
public:
    typedef xoshinro_engine<uint32_t, is_plus, 0, 9, 11> base_type;
    typedef typename base_type::result_type result_type;
    typedef typename base_type::seed_type seed_type;

public:
    xoshinro_engine_128() {
    }

    xoshinro_engine_128(result_type s)
        : base_type(s) {
    }

    using base_type::jump;

    /**
     * @brief just like call next() for 2^64 times
     */
    void jump() {
        static const result_type jump_params[4] = {0x8764000b, 0xf542d2d3, 0x6fa035c3, 0x77f2db5b};
        jump(jump_params);
    }
};

template<bool is_plus>
class xoshinro_engine_256 : public xoshinro_engine<uint64_t, is_plus, 1, 17, 45> {
public:
    typedef xoshinro_engine<uint64_t, is_plus, 1, 17, 45> base_type;
    typedef typename base_type::result_type result_type;
    typedef typename base_type::seed_type seed_type;

public:
    xoshinro_engine_256() {
    }

    xoshinro_engine_256(result_type s)
        : base_type(s) {
    }

    using base_type::jump;

    /**
     * @brief just like call next() for 2^128 times
     */
    void jump() {
        static const result_type jump_params[4] = {
            0x180ec6d33cfd0aba, 0xd5a61266f0c9392c, 0xa9582618e03fc9aa, 0x39abdc4529b1661c};
        jump(jump_params);
    }

    /**
     * @brief just like call next() for 2^192 times
     */
    void long_jump() {
        static const result_type jump_params[4] = {
            0x76e15d3efefdcbbf, 0xc5004e441c522fb3, 0x77710069854ee241, 0x39109bb02acbe635};
        jump(jump_params);
    }
};

/**
 * the random wrapper class, can supply more high level functions
 * support UniformRandomBitGenerator
 * @see https://en.cppreference.com/w/cpp/named_req/UniformRandomBitGenerator
 * support then RandomFunc in std::random_shuffle 
 * @see https://en.cppreference.com/w/cpp/algorithm/random_shuffle
 */
template<typename engine_type>
class random_manager_wrapper {
public:
    typedef engine_type core_type;
    typedef typename core_type::result_type result_type;

private:
    core_type core_;

public:
    random_manager_wrapper() {
    }

    random_manager_wrapper(result_type rd_seed)
        : core_(rd_seed) {
    }

    inline core_type& get_core() noexcept {
        return core_;
    }

    inline const core_type& get_core() const noexcept {
        return core_;
    }

    /**
     * initialize the random seed
     * @param [in] rd_seed the random seed
     */
    void init_seed(result_type rd_seed) {
        core_.init_seed(rd_seed);
    }

    /**
     * initialize the random seed
     * @note the random range is [first, last)
     * @param [in] first Random number seed hash start position
     * @param [in] last Random number seed hash last position
     */
    template<class it>
    void init_seed(it& first, it last) {
        core_.init_seed(first, last);
    }

    /**
     * generate a random number
     * @return the random number
     */
    result_type random() noexcept {
        return core_();
    }

    /**
     * generate a random number
     * @return the random number
     */
    result_type operator()() noexcept {
        return random();
    }

    /**
     * generate a random number
     * @param [in] lowest
     * @param [in] highest (must bigger than the lowest)
     * @note the random range [lowest, highest)
     * @return the random number
     */
    template<typename output_type>
    output_type random_between(output_type lowest, output_type highest) {
        if (highest <= lowest) {
            return lowest;
        }
        result_type res = (*this)();
        return static_cast<output_type>(res % static_cast<result_type>(highest - lowest)) + lowest;
    }

public:
    // ------------ Support for UniformRandomBitGenerator ------------
    static inline constexpr result_type min() noexcept {
        return std::numeric_limits<result_type>::min();
    }

    static inline constexpr result_type max() noexcept {
        return std::numeric_limits<result_type>::max();
    }

    inline result_type g() noexcept {
        return random();
    }

    // ------------ Support for RandomFunc ------------
    /**
     * generate a random number
     * @return the random number
     */
    inline result_type operator()(result_type mod) noexcept {
        if (0 == mod) {
            return random();
        } else {
            return random() % mod;
        }
    }

    template<typename random_it>
    void shuffle(random_it first, random_it last) {
        std::shuffle(first, last, std::move(*this));
    }
};

// mersenne_twister is the STL random algorithm

// ============== random generator - xoshiro algorithm(use less memory than mersenne_twister, but more little repetend, Randomness is better than taus) ==============
// @see http://xoshiro.di.unimi.it
// repetend : 2^128 − 1
using xoroshiro128_starstar = random_manager_wrapper<xoshinro_engine_128<false>>;
// repetend : 2^128 − 1, One less spin, a little faster
using xoroshiro128_plus = random_manager_wrapper<xoshinro_engine_128<true>>;
// repetend : 2^256 − 1
using xoshiro256_starstar = random_manager_wrapper<xoshinro_engine_256<false>>;
// repetend : 2^256 − 1, One less spin, a little faster
using xoshiro256_plus = random_manager_wrapper<xoshinro_engine_256<true>>;

} // namespace ark
