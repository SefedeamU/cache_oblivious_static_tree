#include "cache_oblivious_static_tree.h"

#include <algorithm>
#include <stdexcept>

CacheObliviousStaticTree::CacheObliviousStaticTree(std::vector<std::int32_t> keys) {
    build(std::move(keys));
}

void CacheObliviousStaticTree::build(std::vector<std::int32_t> keys) {
    clear();
    if (keys.empty()) {
        return;
    }

    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    if (keys.size() > static_cast<std::size_t>(npos)) {
        throw std::length_error("CacheObliviousStaticTree supports at most UINT32_MAX nodes");
    }

    std::vector<BuildNode> build_nodes;
    build_nodes.reserve(keys.size());
    const int root = build_balanced_tree(keys, 0, keys.size(), build_nodes);

    std::vector<int> veb_order;
    veb_order.reserve(build_nodes.size());
    emit_veb_layout(root, build_nodes[root].height, build_nodes, veb_order);

    std::vector<std::uint32_t> remap(build_nodes.size(), npos);
    for (std::uint32_t i = 0; i < veb_order.size(); ++i) {
        remap[static_cast<std::size_t>(veb_order[i])] = i;
    }

    nodes_.resize(veb_order.size());
    for (std::uint32_t i = 0; i < veb_order.size(); ++i) {
        const BuildNode& source = build_nodes[static_cast<std::size_t>(veb_order[i])];
        nodes_[i] = Node{
            source.key,
            source.left == -1 ? npos : remap[static_cast<std::size_t>(source.left)],
            source.right == -1 ? npos : remap[static_cast<std::size_t>(source.right)]
        };
    }
}

void CacheObliviousStaticTree::clear() {
    nodes_.clear();
}

bool CacheObliviousStaticTree::empty() const {
    return nodes_.empty();
}

std::size_t CacheObliviousStaticTree::size() const {
    return nodes_.size();
}

bool CacheObliviousStaticTree::contains(std::int32_t key) const {
    std::uint32_t current = root_index();
    while (current != npos) {
        const Node& node = nodes_[current];
        if (key == node.key) {
            return true;
        }
        current = key < node.key ? node.left : node.right;
    }
    return false;
}

std::pair<bool, std::size_t> CacheObliviousStaticTree::contains_with_block_count(
    std::int32_t key,
    std::size_t block_size_bytes
) const {
    if (block_size_bytes == 0) {
        throw std::invalid_argument("block_size_bytes must be greater than zero");
    }

    std::uint32_t current = root_index();
    std::vector<std::size_t> touched_blocks;
    touched_blocks.reserve(64);

    while (current != npos) {
        const std::size_t byte_offset = static_cast<std::size_t>(current) * sizeof(Node);
        const std::size_t block = byte_offset / block_size_bytes;
        if (std::find(touched_blocks.begin(), touched_blocks.end(), block) == touched_blocks.end()) {
            touched_blocks.push_back(block);
        }

        const Node& node = nodes_[current];
        if (key == node.key) {
            return {true, touched_blocks.size()};
        }
        current = key < node.key ? node.left : node.right;
    }

    return {false, touched_blocks.size()};
}

std::vector<std::int32_t> CacheObliviousStaticTree::inorder_keys() const {
    std::vector<std::int32_t> result;
    result.reserve(nodes_.size());
    append_inorder(root_index(), result);
    return result;
}

const std::vector<CacheObliviousStaticTree::Node>& CacheObliviousStaticTree::nodes() const {
    return nodes_;
}

std::uint32_t CacheObliviousStaticTree::root_index() const {
    return nodes_.empty() ? npos : 0U;
}

int CacheObliviousStaticTree::build_balanced_tree(
    const std::vector<std::int32_t>& sorted_keys,
    std::size_t begin,
    std::size_t end,
    std::vector<BuildNode>& nodes
) {
    if (begin >= end) {
        return -1;
    }

    const std::size_t middle = begin + (end - begin) / 2;
    const int index = static_cast<int>(nodes.size());
    nodes.push_back(BuildNode{sorted_keys[middle], -1, -1, 1});

    const int left = build_balanced_tree(sorted_keys, begin, middle, nodes);
    const int right = build_balanced_tree(sorted_keys, middle + 1, end, nodes);
    nodes[static_cast<std::size_t>(index)].left = left;
    nodes[static_cast<std::size_t>(index)].right = right;

    const int left_height = left == -1 ? 0 : nodes[static_cast<std::size_t>(left)].height;
    const int right_height = right == -1 ? 0 : nodes[static_cast<std::size_t>(right)].height;
    nodes[static_cast<std::size_t>(index)].height = 1 + std::max(left_height, right_height);
    return index;
}

void CacheObliviousStaticTree::collect_roots_at_depth(
    int root,
    int depth,
    const std::vector<BuildNode>& nodes,
    std::vector<int>& roots
) {
    if (root == -1) {
        return;
    }
    if (depth == 0) {
        roots.push_back(root);
        return;
    }

    const BuildNode& node = nodes[static_cast<std::size_t>(root)];
    collect_roots_at_depth(node.left, depth - 1, nodes, roots);
    collect_roots_at_depth(node.right, depth - 1, nodes, roots);
}

void CacheObliviousStaticTree::emit_veb_layout(
    int root,
    int height_limit,
    const std::vector<BuildNode>& nodes,
    std::vector<int>& output
) {
    if (root == -1 || height_limit <= 0) {
        return;
    }
    if (height_limit == 1) {
        output.push_back(root);
        return;
    }

    const int top_height = (height_limit + 1) / 2;
    emit_veb_layout(root, top_height, nodes, output);

    std::vector<int> bottom_roots;
    bottom_roots.reserve(1U << std::min(top_height, 20));
    collect_roots_at_depth(root, top_height, nodes, bottom_roots);
    for (int bottom_root : bottom_roots) {
        emit_veb_layout(
            bottom_root,
            std::min(
                nodes[static_cast<std::size_t>(bottom_root)].height,
                height_limit - top_height
            ),
            nodes,
            output
        );
    }
}

void CacheObliviousStaticTree::append_inorder(
    std::uint32_t current,
    std::vector<std::int32_t>& result
) const {
    if (current == npos) {
        return;
    }

    const Node& node = nodes_[current];
    append_inorder(node.left, result);
    result.push_back(node.key);
    append_inorder(node.right, result);
}
