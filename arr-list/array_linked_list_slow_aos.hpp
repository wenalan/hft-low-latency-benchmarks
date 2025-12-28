#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace arrlist_slow {

// Array-backed doubly linked list using indices instead of pointers.
// Supports O(1) push/pop front/back, insert after, and erase by node handle.
// Uses a generation counter to detect stale handles.
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
        nodes_.resize(capacity);
        free_list_.reserve(capacity);
        for (std::size_t i = capacity; i-- > 0;) {
            free_list_.push_back(static_cast<int>(i));
        }
    }

    std::size_t capacity() const { return nodes_.size(); }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    // Adds an element at the front and returns its handle.
    template <typename... Args>
    NodeHandle emplace_front(Args&&... args) {
        NodeHandle handle = allocate_node(std::forward<Args>(args)...);
        Node& node = nodes_[handle.index];
        node.prev = kNull;
        node.next = head_;
        if (head_ != kNull) {
            nodes_[head_].prev = handle.index;
        } else {
            tail_ = handle.index;
        }
        head_ = handle.index;
        ++size_;
        return handle;
    }

    // Adds an element at the back and returns its handle.
    template <typename... Args>
    NodeHandle emplace_back(Args&&... args) {
        NodeHandle handle = allocate_node(std::forward<Args>(args)...);
        Node& node = nodes_[handle.index];
        node.next = kNull;
        node.prev = tail_;
        if (tail_ != kNull) {
            nodes_[tail_].next = handle.index;
        } else {
            head_ = handle.index;
        }
        tail_ = handle.index;
        ++size_;
        return handle;
    }

    // Inserts a value after the given handle and returns the new handle.
    template <typename... Args>
    NodeHandle emplace_after(const NodeHandle& handle, Args&&... args) {
        ensure_valid_handle(handle);
        const int node_index = handle.index;
        NodeHandle new_handle = allocate_node(std::forward<Args>(args)...);
        Node& node = nodes_[new_handle.index];
        const int old_next = nodes_[node_index].next;
        node.prev = node_index;
        node.next = old_next;
        nodes_[node_index].next = new_handle.index;
        if (old_next != kNull) {
            nodes_[old_next].prev = new_handle.index;
        } else {
            tail_ = new_handle.index;
        }
        ++size_;
        return new_handle;
    }

    // Convenience wrappers preserving previous names.
    NodeHandle push_front(const T& value) { return emplace_front(value); }
    NodeHandle push_back(const T& value) { return emplace_back(value); }
    NodeHandle insert_after(const NodeHandle& handle, const T& value) { return emplace_after(handle, value); }

    // Removes the first element and returns its value.
    T pop_front() {
        if (head_ == kNull) {
            throw std::out_of_range("list is empty");
        }
        const int idx = head_;
        head_ = nodes_[idx].next;
        if (head_ != kNull) {
            nodes_[head_].prev = kNull;
        } else {
            tail_ = kNull;
        }
        --size_;
        T value = std::move(nodes_[idx].value);
        release_node(idx);
        return value;
    }

    // Removes the node after the given handle.
    void erase_after(const NodeHandle& handle) {
        ensure_valid_handle(handle);
        const int node_index = handle.index;
        const int target = nodes_[node_index].next;
        if (target == kNull) {
            throw std::out_of_range("no node exists after the given index");
        }
        const int new_next = nodes_[target].next;
        nodes_[node_index].next = new_next;
        if (new_next != kNull) {
            nodes_[new_next].prev = node_index;
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
        const int prev = nodes_[node_index].prev;
        const int next = nodes_[node_index].next;

        if (prev != kNull) {
            nodes_[prev].next = next;
        } else {
            head_ = next;
        }

        if (next != kNull) {
            nodes_[next].prev = prev;
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
            fn(nodes_[idx].value, idx);
            idx = nodes_[idx].next;
        }
    }

    // Accessor for testing/benchmarking.
    const T& at(int node_index) const {
        ensure_valid_node(node_index);
        return nodes_[node_index].value;
    }

    // Lightweight iteration helpers for tight loops (unchecked).
    int head_index_unchecked() const { return head_; }
    int next_index_unchecked(int node_index) const { return nodes_[node_index].next; }
    const T& value_unchecked(int node_index) const { return nodes_[node_index].value; }

    template <typename Fn>
    void for_each_value_unchecked(Fn&& fn) const {
        for (int idx = head_; idx != kNull; idx = nodes_[idx].next) {
            fn(nodes_[idx].value);
        }
    }

private:
    struct Node {
        T value{};
        int next = kNull;
        int prev = kNull;
        std::uint32_t generation = 0;
    };

    static constexpr int kNull = -1;

    template <typename... Args>
    NodeHandle allocate_node(Args&&... args) {
        if (free_list_.empty()) {
            throw std::overflow_error("no free slots left in the list");
        }
        const int idx = free_list_.back();
        free_list_.pop_back();
        nodes_[idx].value = T(std::forward<Args>(args)...);
        nodes_[idx].next = kNull;
        nodes_[idx].prev = kNull;
        ++nodes_[idx].generation;
        return NodeHandle{idx, nodes_[idx].generation};
    }

    void release_node(int idx) {
        nodes_[idx].next = kNull;
        nodes_[idx].prev = kNull;
        free_list_.push_back(idx);
    }

    void ensure_valid_handle(const NodeHandle& handle) const {
        const int idx = handle.index;
        if (idx < 0 || static_cast<std::size_t>(idx) >= nodes_.size() || nodes_[idx].generation != handle.generation) {
            throw std::out_of_range("node handle is invalid or stale");
        }
    }

    void ensure_valid_node(int idx) const {
        if (idx < 0 || static_cast<std::size_t>(idx) >= nodes_.size()) {
            throw std::out_of_range("node index is invalid");
        }
    }

    std::vector<Node> nodes_;
    std::vector<int> free_list_;
    int head_ = kNull;
    int tail_ = kNull;
    std::size_t size_ = 0;
};

} // namespace arrlist_slow
