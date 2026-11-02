# XXTEA helper

`<ark/crypto/xxtea.hpp>` provides the existing XXTEA format through the `ark::core` CMake target. No runtime plugin is required.

```cpp
#include <string>
#include <ark/crypto/xxtea.hpp>

std::string ciphertext;
std::string plaintext;
if (ark::xxtea::xxtea_encrypt("message", "key", ciphertext)) {
    const bool decoded = ark::xxtea::xxtea_decrypt(ciphertext, "key", plaintext);
}
```

String overloads append on success and return `false` for empty input. Keys use at most 16 bytes: short keys stop at the first zero and are zero-filled; exactly 16 nonzero bytes need no terminator; longer keys truncate to 16 bytes. Ciphertext format is unchanged. Endianness uses `std::endian`; the implementation does not define global `MX` or `DELTA` macros.

Raw overloads accept a zero-terminated short key or 16 readable key bytes. Successful results report their length through `out_len` and require `std::free()`. Null pointers, empty inputs, lengths above `UINT32_MAX` and ciphertext lengths not divisible by four return `nullptr` without changing `out_len`. Prefer string overloads for owning results; their temporary allocation is released even if appending throws.

`check-core` covers exact-size short-key allocations, full-width and embedded-zero keys, binary round trips and output appending. AddressSanitizer also checks that normalization does not read beyond a short key's terminator.
