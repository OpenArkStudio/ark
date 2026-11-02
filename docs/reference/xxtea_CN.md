# XXTEA 工具

`<ark/crypto/xxtea.hpp>` 通过 `ark::core` CMake 目标提供现有 XXTEA 格式，无需加载运行时插件。

```cpp
#include <string>
#include <ark/crypto/xxtea.hpp>

std::string ciphertext;
std::string plaintext;
if (ark::xxtea::xxtea_encrypt("message", "key", ciphertext)) {
    const bool decoded = ark::xxtea::xxtea_decrypt(ciphertext, "key", plaintext);
}
```

字符串重载成功时追加输出，空输入返回 `false`。密钥最多 16 字节：短密钥遇零终止并补零；恰好 16 个非零字节无需终止符，长密钥截取前 16 字节。密文格式不变。字节序使用 `std::endian` 判断，实现不再定义全局 `MX` 或 `DELTA` 宏。

原始重载接受带零终止符的短密钥或至少 16 个可读密钥字节；成功后经 `out_len` 返回长度，由调用方 `std::free()`。空指针、空输入、超过 `UINT32_MAX` 的长度，以及非四字节整数倍的密文长度均返回 `nullptr`，不改动 `out_len`。拥有型结果优先用字符串重载；追加字符串抛出异常时也会释放临时分配。

`check-core` 覆盖精确长度分配的短密钥、完整 16 字节密钥、内嵌零字节、二进制往返及输出追加。AddressSanitizer 同时检查密钥处理是否读过短密钥的终止位置。
