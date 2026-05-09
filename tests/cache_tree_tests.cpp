#include "cache_oblivious_static_tree.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

class PointerBST {
public:
    explicit PointerBST(std::vector<std::int32_t> keys) {
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        size_ = keys.size();
        root_ = build_balanced(keys, 0, keys.size());
    }

    [[nodiscard]] std::size_t size() const {
        return size_;
    }

    [[nodiscard]] bool contains(std::int32_t key) const {
        const Node* current = root_.get();
        while (current != nullptr) {
            if (key == current->key) {
                return true;
            }
            current = key < current->key ? current->left.get() : current->right.get();
        }
        return false;
    }

    [[nodiscard]] std::pair<bool, std::size_t> contains_with_block_count(
        std::int32_t key,
        std::size_t block_size_bytes
    ) const {
        const Node* current = root_.get();
        std::vector<std::uintptr_t> touched_blocks;
        touched_blocks.reserve(64);

        while (current != nullptr) {
            const std::uintptr_t block =
                reinterpret_cast<std::uintptr_t>(current) / static_cast<std::uintptr_t>(block_size_bytes);
            if (std::find(touched_blocks.begin(), touched_blocks.end(), block) == touched_blocks.end()) {
                touched_blocks.push_back(block);
            }

            if (key == current->key) {
                return {true, touched_blocks.size()};
            }
            current = key < current->key ? current->left.get() : current->right.get();
        }

        return {false, touched_blocks.size()};
    }

    [[nodiscard]] std::vector<std::int32_t> inorder_keys() const {
        std::vector<std::int32_t> result;
        result.reserve(size_);
        append_inorder(root_.get(), result);
        return result;
    }

private:
    struct Node {
        explicit Node(std::int32_t value) : key(value) {}

        std::int32_t key{};
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;
    };

    std::unique_ptr<Node> root_;
    std::size_t size_{0};

    static std::unique_ptr<Node> build_balanced(
        const std::vector<std::int32_t>& sorted_keys,
        std::size_t begin,
        std::size_t end
    ) {
        if (begin >= end) {
            return nullptr;
        }
        const std::size_t middle = begin + (end - begin) / 2;
        auto node = std::make_unique<Node>(sorted_keys[middle]);
        node->left = build_balanced(sorted_keys, begin, middle);
        node->right = build_balanced(sorted_keys, middle + 1, end);
        return node;
    }

    static void append_inorder(const Node* node, std::vector<std::int32_t>& result) {
        if (node == nullptr) {
            return;
        }
        append_inorder(node->left.get(), result);
        result.push_back(node->key);
        append_inorder(node->right.get(), result);
    }
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::int32_t> sorted_unique(std::vector<std::int32_t> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

std::vector<std::int32_t> generate_distinct_int32_keys(std::size_t count, std::uint32_t seed) {
    std::vector<std::int32_t> keys;
    keys.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t value = (static_cast<std::uint32_t>(i) * 2'654'435'761u) ^ seed;
        keys.push_back(static_cast<std::int32_t>(value));
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::vector<std::int32_t> generate_queries(
    const std::vector<std::int32_t>& keys,
    std::size_t query_count,
    std::uint32_t seed
) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> probability(0.0, 1.0);
    std::uniform_int_distribution<std::uint32_t> raw_value;
    std::uniform_int_distribution<std::size_t> key_index(0, keys.size() - 1);

    std::vector<std::int32_t> queries;
    queries.reserve(query_count);
    for (std::size_t i = 0; i < query_count; ++i) {
        if (probability(rng) < 0.5) {
            queries.push_back(keys[key_index(rng)]);
        } else {
            queries.push_back(static_cast<std::int32_t>(raw_value(rng)));
        }
    }
    return queries;
}

void validate_static_tree_links(const CacheObliviousStaticTree& tree) {
    const auto& nodes = tree.nodes();
    if (nodes.empty()) {
        return;
    }

    std::vector<bool> seen(nodes.size(), false);
    std::vector<std::uint32_t> stack{0};
    while (!stack.empty()) {
        const std::uint32_t current = stack.back();
        stack.pop_back();

        require(current < nodes.size(), "node index outside array");
        require(!seen[current], "cycle or duplicate child reference detected");
        seen[current] = true;

        const auto& node = nodes[current];
        if (node.left != CacheObliviousStaticTree::npos) {
            require(nodes[node.left].key < node.key, "left child violates BST order");
            stack.push_back(node.left);
        }
        if (node.right != CacheObliviousStaticTree::npos) {
            require(nodes[node.right].key > node.key, "right child violates BST order");
            stack.push_back(node.right);
        }
    }

    require(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }),
            "not all layout nodes are reachable from root");
}

void test_empty_and_singleton() {
    CacheObliviousStaticTree empty_static;
    require(empty_static.empty(), "empty static tree should be empty");
    require(!empty_static.contains(0), "empty static tree contains value");

    CacheObliviousStaticTree one_static({17});
    PointerBST one_bst({17});
    require(one_static.size() == 1, "singleton static tree has wrong size");
    require(one_bst.size() == 1, "singleton BST has wrong size");
    require(one_static.contains(17), "singleton static tree misses key");
    require(one_bst.contains(17), "singleton BST misses key");
    require(!one_static.contains(16), "singleton static tree false positive");
    validate_static_tree_links(one_static);
}

void test_duplicates_and_order() {
    const std::vector<std::int32_t> input{
        5, 5, 5, -10, 7, 7, 0,
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max(),
        -10, 42, 42
    };

    const std::vector<std::int32_t> expected = sorted_unique(input);
    const CacheObliviousStaticTree static_tree(input);
    const PointerBST pointer_bst(input);

    require(static_tree.size() == expected.size(), "static tree did not deduplicate correctly");
    require(pointer_bst.size() == expected.size(), "pointer BST did not deduplicate correctly");
    require(static_tree.inorder_keys() == expected, "static tree inorder traversal is not sorted keys");
    require(pointer_bst.inorder_keys() == expected, "pointer BST inorder traversal is not sorted keys");
    validate_static_tree_links(static_tree);
}

void test_many_small_shapes_exhaustively() {
    for (std::size_t n = 0; n <= 257; ++n) {
        std::vector<std::int32_t> keys;
        keys.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            keys.push_back(static_cast<std::int32_t>((i * 37) % 509 - 250));
        }

        const std::vector<std::int32_t> expected = sorted_unique(keys);
        const CacheObliviousStaticTree static_tree(keys);
        const PointerBST pointer_bst(keys);
        require(static_tree.inorder_keys() == expected, "small shape static inorder mismatch");
        require(pointer_bst.inorder_keys() == expected, "small shape BST inorder mismatch");
        validate_static_tree_links(static_tree);

        for (int query = -300; query <= 300; ++query) {
            const bool expected_found =
                std::binary_search(expected.begin(), expected.end(), static_cast<std::int32_t>(query));
            require(static_tree.contains(query) == expected_found, "small shape static contains mismatch");
            require(pointer_bst.contains(query) == expected_found, "small shape BST contains mismatch");
        }
    }
}

void test_random_medium_against_binary_search() {
    std::mt19937 rng(1234567);
    std::uniform_int_distribution<std::int32_t> values(
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max()
    );

    std::vector<std::int32_t> keys;
    keys.reserve(200'000);
    for (std::size_t i = 0; i < 200'000; ++i) {
        keys.push_back(values(rng));
    }

    const std::vector<std::int32_t> expected = sorted_unique(keys);
    const CacheObliviousStaticTree static_tree(keys);
    const PointerBST pointer_bst(keys);
    validate_static_tree_links(static_tree);

    for (std::size_t i = 0; i < 400'000; ++i) {
        const std::int32_t query = (i % 3 == 0) ? expected[i % expected.size()] : values(rng);
        const bool expected_found = std::binary_search(expected.begin(), expected.end(), query);
        require(static_tree.contains(query) == expected_found, "medium random static contains mismatch");
        require(pointer_bst.contains(query) == expected_found, "medium random BST contains mismatch");
    }
}

void test_one_million_elements_and_queries() {
    constexpr std::size_t n = 1'000'000;
    constexpr std::size_t q = 1'000'000;

    const std::vector<std::int32_t> keys = generate_distinct_int32_keys(n, 0xBAD5EEDu);
    const std::vector<std::int32_t> queries = generate_queries(keys, q, 0x12345678u);
    const CacheObliviousStaticTree static_tree(keys);
    const PointerBST pointer_bst(keys);

    validate_static_tree_links(static_tree);
    require(static_tree.size() == n, "large static tree size mismatch");
    require(pointer_bst.size() == n, "large BST size mismatch");

    std::size_t static_hits = 0;
    std::size_t bst_hits = 0;
    std::size_t binary_hits = 0;
    for (std::int32_t query : queries) {
        const auto [static_found, static_blocks] = static_tree.contains_with_block_count(query, 64);
        const auto [bst_found, bst_blocks] = pointer_bst.contains_with_block_count(query, 64);
        const bool expected_found = std::binary_search(keys.begin(), keys.end(), query);

        static_hits += static_found ? 1U : 0U;
        bst_hits += bst_found ? 1U : 0U;
        binary_hits += expected_found ? 1U : 0U;
        require(static_blocks > 0 && bst_blocks > 0, "block counter did not report touches");
    }

    require(static_hits == binary_hits, "large static tree hit count mismatch");
    require(bst_hits == binary_hits, "large BST hit count mismatch");
}

void run_test(const std::string& name, void (*test)()) {
    std::cout << "[ RUN      ] " << name << '\n';
    test();
    std::cout << "[       OK ] " << name << '\n';
}

int main() {
    try {
        run_test("empty_and_singleton", test_empty_and_singleton);
        run_test("duplicates_and_order", test_duplicates_and_order);
        run_test("many_small_shapes_exhaustively", test_many_small_shapes_exhaustively);
        run_test("random_medium_against_binary_search", test_random_medium_against_binary_search);
        run_test("one_million_elements_and_queries", test_one_million_elements_and_queries);
    } catch (const std::exception& error) {
        std::cerr << "[  FAILED  ] " << error.what() << '\n';
        return 1;
    }

    std::cout << "[  PASSED  ] all cache-oblivious static tree tests\n";
    return 0;
}
