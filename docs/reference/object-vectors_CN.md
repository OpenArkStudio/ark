# 对象指针容器与所有权

包含 `<ark/container/vector.hpp>`，通过 CMake 的 `ark::core` 目标引入使用要求。
`pointer_vector<T>` 独占所存对象，`shared_vector<T>` 存储 `std::shared_ptr<T>`。
`value_vector<T>` 仍是 `std::vector<T>` 的别名。

## 所有权

| 操作 | `pointer_vector<T>` | `shared_vector<T>` |
| --- | --- | --- |
| 复制 | 禁止复制；转移所有权时使用移动 | 复制共享引用 |
| 移动 | 转移所有权，源容器变空 | 转移所存引用，源容器变空 |
| 替换槽位 | 删除原对象；同一槽位写入同一指针时保持不变 | 释放原共享引用 |
| `erase(index)` | 删除对象，保留空槽位 | 释放共享引用，保留空槽位 |
| `clear()` 或析构 | 删除全部拥有的对象 | 释放全部所存引用 |

自移动赋值保持容器不变。

非空裸指针只能由一个槽位拥有，不得放入其他槽位／拥有型容器，或手动删除通过 `at()`／迭代器借用的指针。用容器方法替换／清空；迭代器直接赋值会绕过所有权处理。对象通过标量 C++ `delete` 释放，须能经 `T*` 删除。

插入成功才转移所有权；追加分配抛异常时仍归调用方。用 `unique_ptr` 暂存至插入返回：

```cpp
#include <memory>
#include <utility>

#include <ark/container/vector.hpp>

struct item
{
    int id;
};

int main() {
    ark::pointer_vector<item> objects;
    auto pending = std::make_unique<item>(item{7});
    objects.push_back(pending.get());
    pending.release();

    auto transferred = std::move(objects);
    transferred.erase(0); // 删除 item 7，保留空槽位。

    auto external = std::make_shared<item>(item{8});
    ark::shared_vector<item> shared;
    shared.push_back(external);
    auto copy = shared;
    shared.clear();
    copy.clear(); // external 仍然拥有 item 8。
}
```

## 槽位与迭代

| 槽位操作 | 行为 |
| --- | --- |
| `size()`／`resize(n)` | 大小包含空槽；只扩容并置空，不缩容 |
| `push_back(value)` | 填第一个空槽，无空槽则追加 |
| `push_back(index, value)` | 替换已有槽；索引不小于 `size()` 时仅追加一槽；指定索引前先 `resize()` |
| `at(index)` | 越界返回空；`operator[]`／`erase(index)` 要求有效索引 |
| 迭代 | 正反向均访问空槽；只读用 `cbegin()`／`cend()`、`crbegin()`／`crend()` |

容器访问由调用方同步。

## 迁移

转移独占所有权时，将 `pointer_vector` 的复制改为 `std::move`。
多个容器需要共享对象时使用 `shared_vector`，其元素与迭代器值类型为 `std::shared_ptr<T>`；
裸指针版本存储 `T*`。按值存储对象时，继续使用 `value_vector` 或 `std::vector`。

拥有裸指针的向量和映射容器现在直接使用 C++ 标量 `delete`，不再应用 `ARK_DELETE` 覆盖。
需要自定义释放时，使用持有带定制删除器的 `std::shared_ptr` 的共享容器，或存储智能指针的标准容器。
`buffer` 的分配和释放钩子 `ARK_NEW_ARRAY` / `ARK_DELETE_ARRAY` 保持不变，
参见[收包缓冲的所有权](network_CN.md#收包缓冲的所有权)。

## 相关对象映射容器

包含 `<ark/container/map.hpp>` 可使用 `object_map<K, T>` 和
`object_hashmap<K, T>`。`insert(key, pointer)` 成功后才转移所有权；键重复导致插入被拒绝时，
传入的指针仍由调用方拥有。`erase(key)`、`erase(iterator)`、`clear()` 和析构通过标量
`delete` 释放拥有的对象。每个对象只能由一个条目拥有；查找或迭代返回的指针是借用指针。
通过迭代器直接给裸指针赋值会绕过所有权处理。

拥有裸指针的映射容器禁止复制。移动时转移全部条目，并清空源容器；移动赋值先释放目标容器原先拥有的对象。
自移动赋值保持容器不变。`shared_object_map<K, T>` 和 `shared_object_hashmap<K, T>`
保留共享复制语义，并采用相同的移动行为；移除时只释放容器持有的引用。

这是裸指针映射容器的源码兼容性变更：需要转移所有权时，将原先的浅复制改为 `std::move`；
多个独立容器需要共同持有对象时，改用共享映射容器。使用这些公共模板的消费者需要重新编译。
容器不提供深复制操作。
