#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <list>
#include <random>
#include <string>
#include <vector>

#include "array_linked_list_slow_aos.hpp"
#include "array_linked_list_fast_soa.hpp"

template <typename T>
using SlowArrayLinkedList = arrlist_slow::ArrayLinkedList<T>;

template <typename T>
using FastArrayLinkedList = arrlist_fast::ArrayLinkedList<T>;

struct Order {
    std::uint64_t id;
    std::int32_t qty;
};

struct BenchmarkResult {
    std::string name;
    std::size_t operations = 0;
    std::size_t final_depth = 0;
    double ms = 0.0;
    double ns_per_op = 0.0;
    std::uint64_t checksum = 0;
};

// To keep the compiler from optimizing away iteration work.
volatile std::uint64_t g_sink = 0;

enum class Op { Add, Cancel, Iterate };

struct ChurnStep {
    Op op;
    std::size_t cancel_pos; // valid when op == Cancel
    Order order;            // valid when op == Add
};

template <typename List>
class ArrayListBook {
public:
    explicit ArrayListBook(std::size_t capacity) : list_(capacity) { handles_.reserve(capacity); }

    std::size_t size() const { return handles_.size(); }
    std::size_t capacity() const { return list_.capacity(); }

    void add(const Order& order) {
        auto handle = list_.push_back(order);
        handles_.push_back(handle);
    }

    void cancel_at_position(std::size_t pos) {
        if (pos >= handles_.size()) {
            return;
        }
        const std::size_t last = handles_.size() - 1;
        const auto handle = handles_[pos];
        list_.erase(handle);
        handles_[pos] = handles_[last];
        handles_.pop_back();
    }

    void cancel_random(std::mt19937_64& rng) {
        if (handles_.empty()) {
            return;
        }
        std::uniform_int_distribution<std::size_t> dist(0, handles_.size() - 1);
        const std::size_t pos = dist(rng);
        const auto handle = handles_[pos];
        list_.erase(handle);
        handles_[pos] = handles_.back();
        handles_.pop_back();
    }


    // std::uint64_t iterate_sum() const {
    //     std::uint64_t sum = 0;
    //     list_.for_each([&](const Order& o, int) { sum += static_cast<std::uint64_t>(o.qty); });
    //     return sum;
    // }

    std::uint64_t iterate_sum() const {
        std::uint64_t sum = 0;
        list_.for_each_value_unchecked([&](const Order& o) { sum += static_cast<std::uint64_t>(o.qty); });
        return sum;
    }

private:
    List list_;
    std::vector<typename List::NodeHandle> handles_;
};

class StdListBook {
public:
    using Iterator = std::list<Order>::iterator;

    explicit StdListBook(std::size_t capacity) { handles_.reserve(capacity); }

    std::size_t size() const { return handles_.size(); }

    void add(const Order& order) {
        auto it = orders_.insert(orders_.end(), order);
        handles_.push_back(it);
    }

    void cancel_at_position(std::size_t pos) {
        if (pos >= handles_.size()) {
            return;
        }
        const std::size_t last = handles_.size() - 1;
        Iterator it = handles_[pos];
        orders_.erase(it);
        handles_[pos] = handles_[last];
        handles_.pop_back();
    }

    void cancel_random(std::mt19937_64& rng) {
        if (handles_.empty()) {
            return;
        }
        std::uniform_int_distribution<std::size_t> dist(0, handles_.size() - 1);
        const std::size_t pos = dist(rng);
        Iterator it = handles_[pos];
        orders_.erase(it);
        handles_[pos] = handles_.back();
        handles_.pop_back();
    }

    std::uint64_t iterate_sum() const {
        std::uint64_t sum = 0;
        for (const auto& o : orders_) {
            sum += static_cast<std::uint64_t>(o.qty);
        }
        return sum;
    }

private:
    std::list<Order> orders_;
    std::vector<Iterator> handles_;
};

template <typename Book>
void preload(Book& book, const std::vector<Order>& orders) {
    for (const auto& o : orders) {
        book.add(o);
    }
}

template <typename Book>
BenchmarkResult bench_fill(const std::string& name, Book& book, const std::vector<Order>& orders) {
    const auto start = std::chrono::steady_clock::now();
    for (const auto& o : orders) {
        book.add(o);
    }
    const auto end = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    const double ns_per_op = (ms * 1e6) / static_cast<double>(orders.size());
    g_sink = static_cast<std::uint64_t>(book.size());

    return BenchmarkResult{name, orders.size(), book.size(), ms, ns_per_op, g_sink};
}

template <typename Book>
BenchmarkResult bench_erase(const std::string& name,
                            Book& book,
                            const std::vector<Order>& preload_orders,
                            const std::vector<std::size_t>& cancel_positions) {
    preload(book, preload_orders);

    const auto start = std::chrono::steady_clock::now();
    for (std::size_t pos : cancel_positions) {
        book.cancel_at_position(pos);
    }
    const auto end = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    const double ns_per_op = (ms * 1e6) / static_cast<double>(cancel_positions.size());
    g_sink = static_cast<std::uint64_t>(book.size());

    return BenchmarkResult{name, cancel_positions.size(), book.size(), ms, ns_per_op, g_sink};
}

template <typename Book>
BenchmarkResult bench_churn(const std::string& name,
                            Book& book,
                            const std::vector<Order>& preload_orders,
                            const std::vector<ChurnStep>& steps) {
    preload(book, preload_orders);

    const auto start = std::chrono::steady_clock::now();
    for (const auto& step : steps) {
        if (step.op == Op::Add) {
            book.add(step.order);
        } else {
            book.cancel_at_position(step.cancel_pos);
        }
    }
    const auto end = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    const double ns_per_op = (ms * 1e6) / static_cast<double>(steps.size());
    g_sink = static_cast<std::uint64_t>(book.size());

    return BenchmarkResult{name, steps.size(), book.size(), ms, ns_per_op, g_sink};
}

template <typename Book>
BenchmarkResult bench_iterate(const std::string& name,
                              Book& book,
                              const std::vector<Order>& preload_orders,
                              std::size_t iterations) {
    preload(book, preload_orders);

    // Warm cache with one full pass before timing steady-state iterations.
    g_sink = book.iterate_sum();

    std::uint64_t checksum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        checksum += book.iterate_sum();
    }
    const auto end = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    const double ns_per_op = (ms * 1e6) / static_cast<double>(iterations);
    g_sink = checksum;

    return BenchmarkResult{name, iterations, book.size(), ms, ns_per_op, checksum};
}

struct RunSummary {
    BenchmarkResult best;
    BenchmarkResult worst;
};

template <typename Fn>
RunSummary run_best_and_worst(std::size_t runs, Fn&& fn) {
    RunSummary summary;
    summary.best.ms = std::numeric_limits<double>::max();
    summary.worst.ms = 0.0;
    for (std::size_t i = 0; i < runs; ++i) {
        auto r = fn();
        if (r.ms < summary.best.ms) {
            summary.best = r;
        }
        if (r.ms > summary.worst.ms) {
            summary.worst = r;
        }
    }
    return summary;
}

int main() {
    const std::size_t capacity = 32 * 1024;
    const std::size_t erase_ops = capacity;
    const std::size_t churn_ops = 200'000;
    const std::size_t iterate_loops = 2'000;
    const std::size_t runs_per_case = 5;

    std::mt19937_64 rng_orders(42);
    std::uniform_int_distribution<int> qty_dist(1, 10);

    // Precompute orders for the initial fill (used by all scenarios).
    std::vector<Order> fill_orders(capacity);
    for (std::size_t i = 0; i < capacity; ++i) {
        fill_orders[i] = Order{i + 1, qty_dist(rng_orders)};
    }

    // Precompute erase positions from a full list.
    std::mt19937_64 rng_erase(1337);
    std::uniform_int_distribution<std::size_t> erase_dist;
    std::vector<std::size_t> erase_positions;
    erase_positions.reserve(erase_ops);
    std::size_t depth = capacity;
    for (std::size_t i = 0; i < erase_ops; ++i) {
        erase_dist.param(std::uniform_int_distribution<std::size_t>::param_type{0, depth - 1});
        const std::size_t pos = erase_dist(rng_erase);
        erase_positions.push_back(pos);
        --depth;
    }

    // Precompute churn sequence (random add/erase) starting from full depth.
    std::mt19937_64 rng_churn(7);
    std::bernoulli_distribution add_bias(0.5);
    std::uniform_int_distribution<std::size_t> cancel_dist;
    std::vector<Order> churn_orders(churn_ops);
    for (std::size_t i = 0; i < churn_orders.size(); ++i) {
        churn_orders[i] = Order{capacity + i + 1, qty_dist(rng_orders)};
    }

    std::vector<ChurnStep> churn_steps;
    churn_steps.reserve(churn_ops);
    depth = capacity; // start full
    std::size_t order_idx = 0;
    for (std::size_t i = 0; i < churn_ops; ++i) {
        bool do_add = add_bias(rng_churn);
        if (depth == 0) {
            do_add = true;
        } else if (depth == capacity) {
            do_add = false;
        }

        if (do_add) {
            churn_steps.push_back(ChurnStep{Op::Add, 0, churn_orders[order_idx++]});
            ++depth;
        } else {
            cancel_dist.param(std::uniform_int_distribution<std::size_t>::param_type{0, depth - 1});
            const std::size_t pos = cancel_dist(rng_churn);
            churn_steps.push_back(ChurnStep{Op::Cancel, pos, Order{}});
            --depth;
        }
    }

    auto print = [](const BenchmarkResult& r, const std::string& tag) {
        std::cout << "  " << r.name << " [" << tag << "]\n"
                  << "    final depth: " << r.final_depth << "\n"
                  << "    time:        " << r.ms << " ms\n"
                  << "    ns/op:       " << r.ns_per_op << "\n";
    };

    // Scenario 1: fill to capacity.
    {
        auto slow_result = run_best_and_worst(runs_per_case, [&] {
            ArrayListBook<SlowArrayLinkedList<Order>> slow_book(capacity);
            return bench_fill("slow aos fill", slow_book, fill_orders);
        });
        auto fast_result = run_best_and_worst(runs_per_case, [&] {
            ArrayListBook<FastArrayLinkedList<Order>> fast_book(capacity);
            return bench_fill("fast soa fill", fast_book, fill_orders);
        });
        auto list_result = run_best_and_worst(runs_per_case, [&] {
            StdListBook list_book(capacity);
            return bench_fill("std::list fill", list_book, fill_orders);
        });

        std::cout << "Fill to capacity (" << capacity << " orders, best/worst of " << runs_per_case << ")\n";
        print(slow_result.best, "best");
        print(slow_result.worst, "worst");
        print(fast_result.best, "best");
        print(fast_result.worst, "worst");
        print(list_result.best, "best");
        print(list_result.worst, "worst");
        std::cout << "\n";
    }

    // Scenario 2: random erase from full depth.
    {
        auto slow_result = run_best_and_worst(runs_per_case, [&] {
            ArrayListBook<SlowArrayLinkedList<Order>> slow_book(capacity);
            return bench_erase("slow aos erase", slow_book, fill_orders, erase_positions);
        });
        auto fast_result = run_best_and_worst(runs_per_case, [&] {
            ArrayListBook<FastArrayLinkedList<Order>> fast_book(capacity);
            return bench_erase("fast soa erase", fast_book, fill_orders, erase_positions);
        });
        auto list_result = run_best_and_worst(runs_per_case, [&] {
            StdListBook list_book(capacity);
            return bench_erase("std::list erase", list_book, fill_orders, erase_positions);
        });

        std::cout << "Random erase from full depth (" << erase_ops << " cancels, best/worst of " << runs_per_case << ")\n";
        print(slow_result.best, "best");
        print(slow_result.worst, "worst");
        print(fast_result.best, "best");
        print(fast_result.worst, "worst");
        print(list_result.best, "best");
        print(list_result.worst, "worst");
        std::cout << "\n";
    }

    // Scenario 3: churn (random erase + insert) starting from full depth.
    {
        auto slow_result = run_best_and_worst(runs_per_case, [&] {
            ArrayListBook<SlowArrayLinkedList<Order>> slow_book(capacity);
            return bench_churn("slow aos churn", slow_book, fill_orders, churn_steps);
        });
        auto fast_result = run_best_and_worst(runs_per_case, [&] {
            ArrayListBook<FastArrayLinkedList<Order>> fast_book(capacity);
            return bench_churn("fast soa churn", fast_book, fill_orders, churn_steps);
        });
        auto list_result = run_best_and_worst(runs_per_case, [&] {
            StdListBook list_book(capacity);
            return bench_churn("std::list churn", list_book, fill_orders, churn_steps);
        });

        std::cout << "Random erase/insert churn (" << churn_ops << " ops, best/worst of " << runs_per_case << ")\n";
        print(slow_result.best, "best");
        print(slow_result.worst, "worst");
        print(fast_result.best, "best");
        print(fast_result.worst, "worst");
        print(list_result.best, "best");
        print(list_result.worst, "worst");
        std::cout << "\n";
    }

    // Scenario 4: pure iteration over a full bucket.
    {
        auto slow_result = run_best_and_worst(runs_per_case, [&] {
            ArrayListBook<SlowArrayLinkedList<Order>> slow_book(capacity);
            return bench_iterate("slow aos iterate", slow_book, fill_orders, iterate_loops);
        });
        auto fast_result = run_best_and_worst(runs_per_case, [&] {
            ArrayListBook<FastArrayLinkedList<Order>> fast_book(capacity);
            return bench_iterate("fast soa iterate", fast_book, fill_orders, iterate_loops);
        });
        auto list_result = run_best_and_worst(runs_per_case, [&] {
            StdListBook list_book(capacity);
            return bench_iterate("std::list iterate", list_book, fill_orders, iterate_loops);
        });

        std::cout << "Pure iteration over full depth (" << iterate_loops << " traversals, best/worst of " << runs_per_case << ")\n";
        print(slow_result.best, "best");
        print(slow_result.worst, "worst");
        print(fast_result.best, "best");
        print(fast_result.worst, "worst");
        print(list_result.best, "best");
        print(list_result.worst, "worst");
        std::cout << "\n";
    }

    return 0;
}
