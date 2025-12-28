#include "fixed_double.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

bool approx(double a, double b, double eps = 1e-8) {
    return std::fabs(a - b) <= eps;
}

// To avoid the compiler optimizing away the work.
volatile double g_double_sink = 0.0;
volatile std::uint64_t g_fixed_sink = 0;

enum class Operation { Add, Sub, Mul, Div };

struct TickD {
    double bid;
    double ask;
    double qty;
};

struct TickF {
    FixedDouble bid;
    FixedDouble ask;
    FixedDouble qty;
};

std::vector<TickD> make_ticks(std::size_t n) {
    std::vector<TickD> ticks;
    ticks.reserve(n);
    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> price_dist(99.5, 100.5);
    std::uniform_real_distribution<double> qty_dist(0.1, 5.0);
    for (std::size_t i = 0; i < n; ++i) {
        const double bid = price_dist(rng);
        const double ask = bid + 0.01 + 0.01 * (i % 3); // small varying spread
        ticks.push_back(TickD{bid, ask, qty_dist(rng)});
    }
    return ticks;
}

std::vector<TickF> to_fixed(const std::vector<TickD>& src) {
    std::vector<TickF> out;
    out.reserve(src.size());
    for (const auto& t : src) {
        out.push_back(TickF{FixedDouble::from_double(t.bid),
                            FixedDouble::from_double(t.ask),
                            FixedDouble::from_double(t.qty)});
    }
    return out;
}

struct DatumD {
    double num;
    double den;
    double recip;
};

struct DatumF {
    FixedDouble num;
    FixedDouble den;
    FixedDouble recip;
};

std::vector<DatumD> make_double_data(std::size_t n) {
    std::vector<DatumD> data;
    data.reserve(n);
    std::mt19937_64 rng(123);
    std::uniform_real_distribution<double> num_dist(1.0, 10.0);
    std::uniform_real_distribution<double> den_dist(0.1, 5.0); // keep away from zero
    for (std::size_t i = 0; i < n; ++i) {
        const double den = den_dist(rng);
        const double num = num_dist(rng);
        data.push_back(DatumD{num, den, 1.0 / den});
    }
    return data;
}

std::vector<DatumF> make_fixed_data(const std::vector<DatumD>& src) {
    std::vector<DatumF> out;
    out.reserve(src.size());
    for (const auto& d : src) {
        out.push_back(DatumF{FixedDouble::from_double(d.num),
                             FixedDouble::from_double(d.den),
                             FixedDouble::from_double(d.recip)});
    }
    return out;
}

struct Result {
    std::string name;
    double ms = 0.0;
    double ns_per_op = 0.0;
};

template <Operation Op>
Result bench_double_t(const std::vector<TickD>& ticks, std::size_t iters, std::string name) {
    double acc = 0.0;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) {
        const auto& t = ticks[i & (ticks.size() - 1)];
        if constexpr (Op == Operation::Add) acc += (t.bid + t.ask);
        if constexpr (Op == Operation::Sub) acc += (t.ask - t.bid);
        if constexpr (Op == Operation::Mul) acc += (t.bid * t.qty);
        if constexpr (Op == Operation::Div) acc += (t.ask / t.qty);
    }
    const auto end = std::chrono::steady_clock::now();
    g_double_sink = acc;
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    return Result{std::move(name), ms, (ms * 1e6) / static_cast<double>(iters)};
}

template <Operation Op>
Result bench_fixed_t(const std::vector<TickF>& ticks, std::size_t iters, std::string name) {
    FixedDouble acc = FixedDouble::zero();
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) {
        const auto& t = ticks[i & (ticks.size() - 1)];
        if constexpr (Op == Operation::Add) acc += (t.bid + t.ask);
        if constexpr (Op == Operation::Sub) acc += (t.ask - t.bid);
        if constexpr (Op == Operation::Mul) acc += (t.bid * t.qty);
        if constexpr (Op == Operation::Div) acc += (t.ask / t.qty);
    }
    const auto end = std::chrono::steady_clock::now();
    g_fixed_sink = acc.raw_value();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    return Result{std::move(name), ms, (ms * 1e6) / static_cast<double>(iters)};
}

Result bench_double_div(const std::vector<DatumD>& data, std::size_t iters) {
    double acc = 0.0;
    const std::size_t mask = data.size() - 1;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) {
        const auto& d = data[i & mask];
        acc += d.num / d.den;
    }
    const auto end = std::chrono::steady_clock::now();
    g_double_sink = acc;
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    return Result{"double: a/b", ms, (ms * 1e6) / static_cast<double>(iters)};
}

Result bench_double_mul_recip(const std::vector<DatumD>& data, std::size_t iters) {
    double acc = 0.0;
    const std::size_t mask = data.size() - 1;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) {
        const auto& d = data[i & mask];
        acc += d.num * d.recip;
    }
    const auto end = std::chrono::steady_clock::now();
    g_double_sink = acc;
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    return Result{"double: a*(1/b)", ms, (ms * 1e6) / static_cast<double>(iters)};
}

Result bench_double_div_const(const std::vector<DatumD>& data, std::size_t iters) {
    double acc = 0.0;
    const std::size_t mask = data.size() - 1;
    constexpr double den = 1000.0;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) {
        acc += data[i & mask].num / den;
    }
    const auto end = std::chrono::steady_clock::now();
    g_double_sink = acc;
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    return Result{"double: a/1000", ms, (ms * 1e6) / static_cast<double>(iters)};
}

Result bench_double_mul_const_small(const std::vector<DatumD>& data, std::size_t iters) {
    double acc = 0.0;
    const std::size_t mask = data.size() - 1;
    constexpr double factor = 0.001;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) {
        acc += data[i & mask].num * factor;
    }
    const auto end = std::chrono::steady_clock::now();
    g_double_sink = acc;
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    return Result{"double: a*0.001", ms, (ms * 1e6) / static_cast<double>(iters)};
}

Result bench_fixed_div(const std::vector<DatumF>& data, std::size_t iters) {
    FixedDouble acc = FixedDouble::zero();
    const std::size_t mask = data.size() - 1;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) {
        const auto& d = data[i & mask];
        acc += d.num / d.den;
    }
    const auto end = std::chrono::steady_clock::now();
    g_fixed_sink = acc.raw_value();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    return Result{"FixedDouble: a/b", ms, (ms * 1e6) / static_cast<double>(iters)};
}

Result bench_fixed_mul_recip(const std::vector<DatumF>& data, std::size_t iters) {
    FixedDouble acc = FixedDouble::zero();
    const std::size_t mask = data.size() - 1;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) {
        const auto& d = data[i & mask];
        acc += d.num * d.recip;
    }
    const auto end = std::chrono::steady_clock::now();
    g_fixed_sink = acc.raw_value();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    return Result{"FixedDouble: a*(1/b)", ms, (ms * 1e6) / static_cast<double>(iters)};
}

Result bench_fixed_div_const(const std::vector<DatumF>& data, std::size_t iters) {
    FixedDouble acc = FixedDouble::zero();
    const std::size_t mask = data.size() - 1;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) {
        acc += data[i & mask].num / 1000;
    }
    const auto end = std::chrono::steady_clock::now();
    g_fixed_sink = acc.raw_value();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    return Result{"FixedDouble: a/1000", ms, (ms * 1e6) / static_cast<double>(iters)};
}

Result bench_fixed_mul_const_small(const std::vector<DatumF>& data, std::size_t iters) {
    FixedDouble acc = FixedDouble::zero();
    const std::size_t mask = data.size() - 1;
    constexpr int64_t k = static_cast<std::int64_t>(std::llround(std::ldexp(0.001, 32 /*fractional_bits*/)));
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) {
        acc += data[i & mask].num * k;
    }
    const auto end = std::chrono::steady_clock::now();
    g_fixed_sink = acc.raw_value();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    return Result{"FixedDouble: a*0.001", ms, (ms * 1e6) / static_cast<double>(iters)};
}

void run_fixed_double_tests() {
    const auto a = FixedDouble::from_double(1.5);
    const auto b = FixedDouble::from_int(2);

    const auto c = a * b;  // 3.0
    assert(approx(c.to_double(), 3.0));

    const auto d = c + FixedDouble::from_double(0.25);  // 3.25
    assert(approx(d.to_double(), 3.25));

    const auto e = d / b;  // 1.625
    assert(approx(e.to_double(), 1.625));

    const auto f = e - FixedDouble::from_double(5.0);  // negative
    assert(f < FixedDouble::zero());

    const auto raw = a.raw_value();
    const auto a2 = FixedDouble::from_raw(raw);
    assert(a == a2);

    const auto big = FixedDouble::from_double(1e12);
    assert(approx(big.to_double(), 1e12, 1e-6));

    const auto huge = FixedDouble::from_double(1e16);
    assert(approx(huge.to_double(), FixedDouble::max_value(), 1.0));

    std::cout << "All FixedDouble checks passed\n";
}

void run_arithmetic_benchmarks() {
    const std::size_t ticks = 64 * 1024;
    const std::size_t iters = 5'000'000; // iterations per operation; 8 benches total

    auto double_ticks = make_ticks(ticks);
    auto fixed_ticks = to_fixed(double_ticks);

    std::vector<Result> results;
    results.push_back(bench_double_t<Operation::Add>(double_ticks, iters, "double add"));
    results.push_back(bench_double_t<Operation::Sub>(double_ticks, iters, "double sub"));
    results.push_back(bench_double_t<Operation::Mul>(double_ticks, iters, "double mul"));
    results.push_back(bench_double_t<Operation::Div>(double_ticks, iters, "double div"));

    results.push_back(bench_fixed_t<Operation::Add>(fixed_ticks, iters, "FixedDouble add"));
    results.push_back(bench_fixed_t<Operation::Sub>(fixed_ticks, iters, "FixedDouble sub"));
    results.push_back(bench_fixed_t<Operation::Mul>(fixed_ticks, iters, "FixedDouble mul"));
    results.push_back(bench_fixed_t<Operation::Div>(fixed_ticks, iters, "FixedDouble div"));

    std::cout << "Arithmetic microbench (per op: " << iters << " iterations)\n";
    for (const auto& r : results) {
        std::cout << "  " << r.name << ": " << r.ms << " ms, " << r.ns_per_op << " ns/op\n";
    }
    std::cout << "sinks: " << g_double_sink << " / " << g_fixed_sink << "\n";
}

void run_division_benchmarks() {
    const std::size_t samples = 16 * 1024;
    const std::size_t iters = 20'000'000;

    auto data_d = make_double_data(samples);
    auto data_f = make_fixed_data(data_d);

    std::vector<Result> results;
    results.push_back(bench_double_div(data_d, iters));
    results.push_back(bench_double_mul_recip(data_d, iters));
    results.push_back(bench_double_div_const(data_d, iters));
    results.push_back(bench_double_mul_const_small(data_d, iters));
    results.push_back(bench_fixed_div(data_f, iters));
    results.push_back(bench_fixed_mul_recip(data_f, iters));
    results.push_back(bench_fixed_div_const(data_f, iters));
    results.push_back(bench_fixed_mul_const_small(data_f, iters));

    std::cout << "Division vs reciprocal multiply (" << iters << " iterations)\n";
    for (const auto& r : results) {
        std::cout << "  " << r.name << ": " << r.ms << " ms, " << r.ns_per_op << " ns/op\n";
    }
    std::cout << "sinks: " << g_double_sink << " / " << g_fixed_sink << "\n";
}

}  // namespace

int main() {
    run_fixed_double_tests();
    run_arithmetic_benchmarks();
    run_division_benchmarks();
    return 0;
}
