#pragma once

// Minimal COM RAII (from foo_mediabar): no implicit AddRef from a raw pointer, put() for
// out-parameters.

#include <utility>

namespace bettertabs {

template <class T>
class com_ptr {
public:
    com_ptr() noexcept = default;
    com_ptr(const com_ptr& other) noexcept : ptr_(other.ptr_) {
        if (ptr_ != nullptr) ptr_->AddRef();
    }
    com_ptr(com_ptr&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}
    ~com_ptr() { reset(); }

    com_ptr& operator=(const com_ptr& other) noexcept {
        if (this != &other) {
            com_ptr copy(other);
            std::swap(ptr_, copy.ptr_);
        }
        return *this;
    }
    com_ptr& operator=(com_ptr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = std::exchange(other.ptr_, nullptr);
        }
        return *this;
    }

    void reset() noexcept {
        if (T* victim = std::exchange(ptr_, nullptr); victim != nullptr) victim->Release();
    }
    [[nodiscard]] T** put() noexcept {
        reset();
        return &ptr_;
    }
    [[nodiscard]] T* get() const noexcept { return ptr_; }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    T* ptr_{nullptr};
};

} // namespace bettertabs
