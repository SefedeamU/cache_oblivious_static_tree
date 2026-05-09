#include "cache_oblivious_static_tree.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

class PointerBST {
public:
    explicit PointerBST(std::vector<std::int32_t> keys) {
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        size_ = keys.size();
        root_ = build_balanced(keys, 0, keys.size());
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
        if (block_size_bytes == 0) {
            throw std::invalid_argument("block_size_bytes must be greater than zero");
        }

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
};

struct ExperimentConfig {
    std::size_t element_count{1'000'000};
    std::size_t query_count{1'000'000};
    int trials{5};
    std::size_t block_size_bytes{64};
    double hit_rate{0.50};
    std::uint32_t seed{0xC0FFEEu};
    std::string results_path{"results.csv"};
    std::string hardware_path{"hardware.txt"};
};

struct TrialResult {
    int trial{};
    std::size_t static_hits{};
    std::size_t bst_hits{};
    double static_ms{};
    double bst_ms{};
    double static_avg_blocks{};
    double bst_avg_blocks{};
};

std::vector<std::int32_t> generate_distinct_int32_keys(std::size_t count, std::uint32_t seed) {
    if (count > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::length_error("cannot generate more than UINT32_MAX distinct int32 keys");
    }

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
    double hit_rate,
    std::uint32_t seed
) {
    if (hit_rate < 0.0 || hit_rate > 1.0) {
        throw std::invalid_argument("hit_rate must be between 0.0 and 1.0");
    }

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> probability(0.0, 1.0);
    std::uniform_int_distribution<std::uint32_t> raw_value;
    std::uniform_int_distribution<std::size_t> key_index(0, keys.empty() ? 0 : keys.size() - 1);

    std::vector<std::int32_t> queries;
    queries.reserve(query_count);
    for (std::size_t i = 0; i < query_count; ++i) {
        if (!keys.empty() && probability(rng) < hit_rate) {
            queries.push_back(keys[key_index(rng)]);
        } else {
            queries.push_back(static_cast<std::int32_t>(raw_value(rng)));
        }
    }
    return queries;
}

std::string read_first_cpu_model_line() {
#if defined(__linux__)
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.rfind("model name", 0) == 0) {
            const std::size_t colon = line.find(':');
            return colon == std::string::npos ? line : line.substr(colon + 2);
        }
    }
#endif
    return "Unknown CPU";
}

std::uint64_t total_memory_bytes() {
#if defined(__linux__)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page_size);
    }
#endif
    return 0;
}

void write_hardware_description(const std::string& path) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("could not open hardware output file: " + path);
    }

    const std::uint64_t memory = total_memory_bytes();
    out << "CPU: " << read_first_cpu_model_line() << '\n';
    out << "Logical cores: " << std::thread::hardware_concurrency() << '\n';
    if (memory > 0) {
        out << "RAM: " << std::fixed << std::setprecision(2)
            << static_cast<double>(memory) / (1024.0 * 1024.0 * 1024.0) << " GiB\n";
    } else {
        out << "RAM: Unknown\n";
    }
    out << "Notes: timings use std::chrono::steady_clock; trees are built from identical int32 keys.\n";
}

template <typename Tree>
std::tuple<double, std::size_t, double> time_contains_queries(
    const Tree& tree,
    const std::vector<std::int32_t>& queries,
    std::size_t block_size_bytes
) {
    std::size_t hits = 0;
    std::size_t total_blocks = 0;

    const auto begin = std::chrono::steady_clock::now();
    for (std::int32_t query : queries) {
        const auto [found, blocks] = tree.contains_with_block_count(query, block_size_bytes);
        hits += found ? 1U : 0U;
        total_blocks += blocks;
    }
    const auto end = std::chrono::steady_clock::now();

    const double milliseconds = std::chrono::duration<double, std::milli>(end - begin).count();
    const double average_blocks =
        queries.empty() ? 0.0 : static_cast<double>(total_blocks) / static_cast<double>(queries.size());
    return {milliseconds, hits, average_blocks};
}

std::vector<TrialResult> run_experiments(const ExperimentConfig& config) {
    if (config.trials <= 0 || config.element_count == 0 || config.query_count == 0) {
        throw std::invalid_argument("n, queries and trials must be greater than zero");
    }

    const std::vector<std::int32_t> keys =
        generate_distinct_int32_keys(config.element_count, config.seed);
    const CacheObliviousStaticTree static_tree(keys);
    const PointerBST pointer_bst(keys);

    std::vector<TrialResult> results;
    results.reserve(static_cast<std::size_t>(config.trials));

    for (int trial = 1; trial <= config.trials; ++trial) {
        const std::vector<std::int32_t> queries = generate_queries(
            keys,
            config.query_count,
            config.hit_rate,
            config.seed + static_cast<std::uint32_t>(trial * 7919)
        );

        const auto [static_ms, static_hits, static_blocks] =
            time_contains_queries(static_tree, queries, config.block_size_bytes);
        const auto [bst_ms, bst_hits, bst_blocks] =
            time_contains_queries(pointer_bst, queries, config.block_size_bytes);

        if (static_hits != bst_hits) {
            throw std::runtime_error("static tree and pointer BST returned different hit counts");
        }

        results.push_back({trial, static_hits, bst_hits, static_ms, bst_ms, static_blocks, bst_blocks});
    }
    return results;
}

void write_results_csv(const ExperimentConfig& config, const std::vector<TrialResult>& results) {
    std::ofstream out(config.results_path);
    if (!out) {
        throw std::runtime_error("could not open results output file: " + config.results_path);
    }

    out << "trial,n,queries,block_size_bytes,hit_rate,static_tree_ms,pointer_bst_ms,"
        << "static_tree_hits,pointer_bst_hits,static_tree_avg_blocks,pointer_bst_avg_blocks\n";
    for (const TrialResult& result : results) {
        out << result.trial << ','
            << config.element_count << ','
            << config.query_count << ','
            << config.block_size_bytes << ','
            << config.hit_rate << ','
            << result.static_ms << ','
            << result.bst_ms << ','
            << result.static_hits << ','
            << result.bst_hits << ','
            << result.static_avg_blocks << ','
            << result.bst_avg_blocks << '\n';
    }
}

TrialResult average_result(const std::vector<TrialResult>& results) {
    TrialResult average{};
    for (const TrialResult& result : results) {
        average.static_hits += result.static_hits;
        average.bst_hits += result.bst_hits;
        average.static_ms += result.static_ms;
        average.bst_ms += result.bst_ms;
        average.static_avg_blocks += result.static_avg_blocks;
        average.bst_avg_blocks += result.bst_avg_blocks;
    }

    const double count = static_cast<double>(results.size());
    average.static_hits /= results.size();
    average.bst_hits /= results.size();
    average.static_ms /= count;
    average.bst_ms /= count;
    average.static_avg_blocks /= count;
    average.bst_avg_blocks /= count;
    return average;
}

std::size_t parse_size(const std::string& value) {
    return static_cast<std::size_t>(std::stoull(value));
}

void print_usage(const char* program) {
    std::cout
        << "Uso:\n"
        << "  " << program << " [--n 1000000] [--queries 1000000] [--trials 5]\n"
        << "      [--block-size 64] [--hit-rate 0.5] [--seed 12648430]\n"
        << "      [--output results.csv] [--hardware hardware.txt]\n"
        << "  " << program << " --demo\n"
        << "  " << program << " --quick\n";
}

ExperimentConfig parse_arguments(int argc, char** argv, bool& demo) {
    ExperimentConfig config;
    demo = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto require_value = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value for " + name);
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg == "--demo") {
            demo = true;
        } else if (arg == "--quick") {
            config.element_count = 100'000;
            config.query_count = 100'000;
            config.trials = 2;
            config.results_path = "quick_results.csv";
        } else if (arg == "--n") {
            config.element_count = parse_size(require_value(arg));
        } else if (arg == "--queries") {
            config.query_count = parse_size(require_value(arg));
        } else if (arg == "--trials") {
            config.trials = std::stoi(require_value(arg));
        } else if (arg == "--block-size") {
            config.block_size_bytes = parse_size(require_value(arg));
        } else if (arg == "--hit-rate") {
            config.hit_rate = std::stod(require_value(arg));
        } else if (arg == "--seed") {
            config.seed = static_cast<std::uint32_t>(std::stoul(require_value(arg)));
        } else if (arg == "--output") {
            config.results_path = require_value(arg);
        } else if (arg == "--hardware") {
            config.hardware_path = require_value(arg);
        } else {
            throw std::invalid_argument("unknown argument: " + arg);
        }
    }

    return config;
}

void run_demo() {
    const std::vector<std::int32_t> values{42, 7, -9, 100, 13, 0, 7, 88, 31};
    const CacheObliviousStaticTree static_tree(values);
    const PointerBST pointer_bst(values);

    std::cout << "Demo con claves int32, duplicados eliminados:\n";
    std::cout << "Elementos en orden: ";
    for (std::int32_t key : static_tree.inorder_keys()) {
        std::cout << key << ' ';
    }
    std::cout << "\n\n";

    for (std::int32_t query : {-9, 7, 8, 42, 101}) {
        std::cout << "buscar " << std::setw(4) << query
                  << " | static_tree=" << (static_tree.contains(query) ? "si" : "no")
                  << " | pointer_bst=" << (pointer_bst.contains(query) ? "si" : "no") << '\n';
    }
}

int main(int argc, char** argv) {
    try {
        bool demo = false;
        const ExperimentConfig config = parse_arguments(argc, argv, demo);

        if (demo) {
            run_demo();
            return 0;
        }

        std::cout << "Construyendo ambos arboles con " << config.element_count
                  << " elementos int32...\n";
        std::cout << "Consultas por experimento: " << config.query_count
                  << ", T=" << config.trials
                  << ", B=" << config.block_size_bytes << " bytes\n";

        write_hardware_description(config.hardware_path);
        const std::vector<TrialResult> results = run_experiments(config);
        write_results_csv(config, results);
        const TrialResult average = average_result(results);

        std::cout << std::fixed << std::setprecision(3);
        for (const TrialResult& result : results) {
            std::cout << "Trial " << result.trial
                      << " | static_tree=" << result.static_ms << " ms"
                      << " | pointer_bst=" << result.bst_ms << " ms"
                      << " | hits=" << result.static_hits << '\n';
        }

        std::cout << "\nPromedio de " << results.size() << " experimentos:\n";
        std::cout << "  Cache-oblivious static tree: " << average.static_ms
                  << " ms, bloques promedio=" << average.static_avg_blocks << '\n';
        std::cout << "  BST con punteros:           " << average.bst_ms
                  << " ms, bloques promedio=" << average.bst_avg_blocks << '\n';
        std::cout << "Resultados guardados en: " << config.results_path << '\n';
        std::cout << "Hardware guardado en: " << config.hardware_path << '\n';
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n\n";
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}
