#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

// Array-backed doubly linked list using indices instead of pointers.
// Storage uses structure-of-arrays (SoA) for better cache behavior when only
// some fields are touched during traversal. Operations use stable handles
// (index + generation) to detect stale references.
template <typename T>
class ArrayLinkedList {
public:
    struct NodeHandle {
        int index = -1;
        std::uint32_t generation = 0;
    };

    explicit ArrayLinkedList(std::size_t capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("capacity must be greater than zero");
        }
        values_.resize(capacity);
        next_.assign(capacity, kNull);
        prev_.assign(capacity, kNull);
        generations_.assign(capacity, 0);
        free_list_.reserve(capacity);
        for (std::size_t i = capacity; i-- > 0;) {
            free_list_.push_back(static_cast<int>(i));
        }
    }

    std::size_t capacity() const { return values_.size(); }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    // Adds an element at the front and returns its handle.
    template <typename... Args>
    NodeHandle emplace_front(Args&&... args) {
        NodeHandle handle = allocate_node(std::forward<Args>(args)...);
        const int idx = handle.index;
        prev_[idx] = kNull;
        next_[idx] = head_;
        if (head_ != kNull) {
            prev_[head_] = idx;
        } else {
            tail_ = idx;
        }
        head_ = idx;
        ++size_;
        return handle;
    }

    // Adds an element at the back and returns its handle.
    template <typename... Args>
    NodeHandle emplace_back(Args&&... args) {
        NodeHandle handle = allocate_node(std::forward<Args>(args)...);
        const int idx = handle.index;
        next_[idx] = kNull;
        prev_[idx] = tail_;
        if (tail_ != kNull) {
            next_[tail_] = idx;
        } else {
            head_ = idx;
        }
        tail_ = idx;
        ++size_;
        return handle;
    }

    // Inserts a value after the given handle and returns the new handle.
    template <typename... Args>
    NodeHandle emplace_after(const NodeHandle& handle, Args&&... args) {
        ensure_valid_handle(handle);
        const int node_index = handle.index;
        NodeHandle new_handle = allocate_node(std::forward<Args>(args)...);
        const int idx = new_handle.index;
        const int old_next = next_[node_index];
        prev_[idx] = node_index;
        next_[idx] = old_next;
        next_[node_index] = idx;
        if (old_next != kNull) {
            prev_[old_next] = idx;
        } else {
            tail_ = idx;
        }
        ++size_;
        return new_handle;
    }

    // Removes the first element and returns its value.
    T pop_front() {
        if (head_ == kNull) {
            throw std::out_of_range("list is empty");
        }
        const int idx = head_;
        head_ = next_[idx];
        if (head_ != kNull) {
            prev_[head_] = kNull;
        } else {
            tail_ = kNull;
        }
        --size_;
        T value = std::move(values_[idx]);
        release_node(idx);
        return value;
    }

    // Removes the node after the given handle.
    void erase_after(const NodeHandle& handle) {
        ensure_valid_handle(handle);
        const int node_index = handle.index;
        const int target = next_[node_index];
        if (target == kNull) {
            throw std::out_of_range("no node exists after the given index");
        }
        const int new_next = next_[target];
        next_[node_index] = new_next;
        if (new_next != kNull) {
            prev_[new_next] = node_index;
        } else {
            tail_ = node_index;
        }
        --size_;
        release_node(target);
    }

    // Removes a node by handle in O(1) time.
    void erase(const NodeHandle& handle) {
        ensure_valid_handle(handle);
        const int node_index = handle.index;
        const int prev = prev_[node_index];
        const int next = next_[node_index];

        if (prev != kNull) {
            next_[prev] = next;
        } else {
            head_ = next;
        }

        if (next != kNull) {
            prev_[next] = prev;
        } else {
            tail_ = prev;
        }

        --size_;
        release_node(node_index);
    }

    // Iterates through the list, calling fn(value, index) for each element.
    template <typename Fn>
    void for_each(Fn&& fn) const {
        int idx = head_;
        while (idx != kNull) {
            fn(values_[idx], idx);
            idx = next_[idx];
        }
    }

    // Accessor for testing/benchmarking.
    const T& at(int node_index) const {
        ensure_valid_node(node_index);
        return values_[node_index];
    }

    // Convenience push/insert wrappers preserving previous API names.
    NodeHandle push_front(const T& value) { return emplace_front(value); }
    NodeHandle push_back(const T& value) { return emplace_back(value); }
    NodeHandle insert_after(const NodeHandle& handle, const T& value) { return emplace_after(handle, value); }

    // Lightweight iteration helpers for tight loops (unchecked).
    int head_index_unchecked() const { return head_; }
    int next_index_unchecked(int node_index) const { return next_[node_index]; }
    const T& value_unchecked(int node_index) const { return values_[node_index]; }

    template <typename Fn>
    void for_each_value_unchecked(Fn&& fn) const {
        for (int idx = head_; idx != kNull; idx = next_[idx]) {
            fn(values_[idx]);
        }
    }

private:
    static constexpr int kNull = -1;

    template <typename... Args>
    NodeHandle allocate_node(Args&&... args) {
        if (free_list_.empty()) {
            throw std::overflow_error("no free slots left in the list");
        }
        const int idx = free_list_.back();
        free_list_.pop_back();
        values_[idx] = T(std::forward<Args>(args)...);
        next_[idx] = kNull;
        prev_[idx] = kNull;
        ++generations_[idx];
        return NodeHandle{idx, generations_[idx]};
    }

    void release_node(int idx) {
        next_[idx] = kNull;
        prev_[idx] = kNull;
        free_list_.push_back(idx);
    }

    void ensure_valid_handle(const NodeHandle& handle) const {
        const int idx = handle.index;
        if (idx < 0 || static_cast<std::size_t>(idx) >= values_.size() || generations_[idx] != handle.generation) {
            throw std::out_of_range("node handle is invalid or stale");
        }
    }

    void ensure_valid_node(int idx) const {
        if (idx < 0 || static_cast<std::size_t>(idx) >= values_.size()) {
            throw std::out_of_range("node index is invalid");
        }
    }

    std::vector<T> values_;
    std::vector<int> next_;
    std::vector<int> prev_;
    std::vector<std::uint32_t> generations_;
    std::vector<int> free_list_;
    int head_ = kNull;
    int tail_ = kNull;
    std::size_t size_ = 0;
};
