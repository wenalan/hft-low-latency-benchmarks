#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <ostream>
#include <stdexcept>

// FixedDouble implements a signed fixed-decimal number with three fractional
// digits. It stores values as an int64_t scaled by 1000 (value * 1000) which
// keeps arithmetic simple and predictable for currency-like values.
class FixedDouble {
public:
    using storage_type = std::int64_t;

    static constexpr storage_type scale = 1000;
    static constexpr double inv_scale = 1.0 / static_cast<double>(scale);

    constexpr FixedDouble() = default;

    static constexpr FixedDouble from_raw(storage_type raw) { return FixedDouble(raw); }

    static constexpr FixedDouble from_int(std::int64_t value) {
        return FixedDouble(to_raw(saturate_to_signed(static_cast<wide_type>(value) * scale)));
    }

    static FixedDouble from_double(double value) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("FixedDouble cannot represent NaN or infinity");
        }
        const double scaled = value * static_cast<double>(scale);
        const wide_type rounded = static_cast<wide_type>(std::llround(scaled));
        return FixedDouble(to_raw(saturate_to_signed(rounded)));
    }

    double to_double() const { return static_cast<double>(raw_) * inv_scale; }
    std::int64_t to_int64() const { return raw_ / scale; }
    storage_type raw_value() const { return raw_; }

    // Arithmetic
    FixedDouble& operator+=(FixedDouble other) {
        //raw_ = saturating_add(raw_, other.raw_);
        raw_ = raw_ + other.raw_;
        return *this;
    }

    FixedDouble& operator-=(FixedDouble other) {
        //raw_ = saturating_sub(raw_, other.raw_);
        raw_ = raw_ - other.raw_;
        return *this;
    }

    FixedDouble& operator*=(FixedDouble other) {
        raw_ = saturating_mul(raw_, other.raw_);
        return *this;
    }

    FixedDouble& operator/=(FixedDouble other) {
        raw_ = saturating_div(raw_, other.raw_);
        return *this;
    }

    friend FixedDouble operator+(FixedDouble lhs, FixedDouble rhs) {
        lhs += rhs;
        return lhs;
    }

    friend FixedDouble operator-(FixedDouble lhs, FixedDouble rhs) {
        lhs -= rhs;
        return lhs;
    }

    FixedDouble operator/(int k) const {
        if (k == 0) {
            throw std::overflow_error("FixedDouble divide by zero");
        }
        const wide_type res = raw_ / k;
        return FixedDouble::from_raw(res);
    }

    FixedDouble operator*(int64_t k) const {
        const wide_type prod = static_cast<wide_type>(raw_) * static_cast<wide_type>(k);
        return from_raw(saturate_to_signed(prod));
    }

    friend FixedDouble operator*(FixedDouble lhs, FixedDouble rhs) {
        lhs *= rhs;
        return lhs;
    }

    friend FixedDouble operator/(FixedDouble lhs, FixedDouble rhs) {
        lhs /= rhs;
        return lhs;
    }

    // Comparisons use the signed interpretation of the raw bits.
    friend bool operator==(FixedDouble lhs, FixedDouble rhs) { return lhs.raw_ == rhs.raw_; }
    friend bool operator!=(FixedDouble lhs, FixedDouble rhs) { return !(lhs == rhs); }
    friend bool operator<(FixedDouble lhs, FixedDouble rhs) { return lhs.raw_ < rhs.raw_; }
    friend bool operator<=(FixedDouble lhs, FixedDouble rhs) { return lhs.raw_ <= rhs.raw_; }
    friend bool operator>(FixedDouble lhs, FixedDouble rhs) { return rhs < lhs; }
    friend bool operator>=(FixedDouble lhs, FixedDouble rhs) { return rhs <= lhs; }

    // Convenience values and limits.
    static constexpr FixedDouble zero() { return FixedDouble(); }
    static constexpr FixedDouble one() { return from_int(1); }
    static constexpr double max_value() { return static_cast<double>(kMaxRaw) * inv_scale; }
    static constexpr double min_value() { return static_cast<double>(kMinRaw) * inv_scale; }

private:
    using wide_type = __int128;

    static constexpr storage_type kMaxRaw = std::numeric_limits<storage_type>::max();
    static constexpr storage_type kMinRaw = std::numeric_limits<storage_type>::min();

    explicit constexpr FixedDouble(storage_type raw) : raw_(raw) {}

    static constexpr storage_type to_raw(storage_type value) { return value; }

    static constexpr storage_type saturate_to_signed(wide_type value) {
        if (value > static_cast<wide_type>(kMaxRaw)) {
            return kMaxRaw;
        }
        if (value < static_cast<wide_type>(kMinRaw)) {
            return kMinRaw;
        }
        return static_cast<storage_type>(value);
    }

    static constexpr storage_type saturating_add(storage_type a, storage_type b) {
        return saturate_to_signed(static_cast<wide_type>(a) + static_cast<wide_type>(b));
    }

    static constexpr storage_type saturating_sub(storage_type a, storage_type b) {
        return saturate_to_signed(static_cast<wide_type>(a) - static_cast<wide_type>(b));
    }

    static constexpr storage_type saturating_mul(storage_type a, storage_type b) {
        const wide_type prod = static_cast<wide_type>(a) * static_cast<wide_type>(b);
        return saturate_to_signed(prod / static_cast<wide_type>(scale));
    }

    static storage_type saturating_div(storage_type num, storage_type den) {
        if (den == 0) {
            throw std::overflow_error("FixedDouble divide by zero");
        }
        const wide_type numerator = static_cast<wide_type>(num) * static_cast<wide_type>(scale);
        return saturate_to_signed(numerator / static_cast<wide_type>(den));
    }

    storage_type raw_{0};
};

inline std::ostream& operator<<(std::ostream& os, const FixedDouble& v) {
    return os << v.to_double();
}
