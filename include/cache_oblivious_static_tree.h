#pragma once

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

class CacheObliviousStaticTree {
public:
    struct Node {
        std::int32_t key{};
        std::uint32_t left{npos};
        std::uint32_t right{npos};
    };

    static constexpr std::uint32_t npos = std::numeric_limits<std::uint32_t>::max();

    CacheObliviousStaticTree() = default;
    explicit CacheObliviousStaticTree(std::vector<std::int32_t> keys);

    void build(std::vector<std::int32_t> keys);
    void clear();

    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] bool contains(std::int32_t key) const;

    [[nodiscard]] std::pair<bool, std::size_t> contains_with_block_count(
        std::int32_t key,
        std::size_t block_size_bytes
    ) const;

    [[nodiscard]] std::vector<std::int32_t> inorder_keys() const;
    [[nodiscard]] const std::vector<Node>& nodes() const;

private:
    struct BuildNode {
        std::int32_t key{};
        int left{-1};
        int right{-1};
        int height{1};
    };

    std::vector<Node> nodes_;

    [[nodiscard]] std::uint32_t root_index() const;

    static int build_balanced_tree(
        const std::vector<std::int32_t>& sorted_keys,
        std::size_t begin,
        std::size_t end,
        std::vector<BuildNode>& nodes
    );

    static void collect_roots_at_depth(
        int root,
        int depth,
        const std::vector<BuildNode>& nodes,
        std::vector<int>& roots
    );

    static void emit_veb_layout(
        int root,
        int height_limit,
        const std::vector<BuildNode>& nodes,
        std::vector<int>& output
    );

    void append_inorder(std::uint32_t current, std::vector<std::int32_t>& result) const;
};
