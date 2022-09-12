#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;

    RawMemory(RawMemory&& other) noexcept {
        capacity_ = std::exchange(other.capacity_, 0);
        buffer_ = std::exchange(other.buffer_, nullptr);
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept {
        capacity_ = std::exchange(rhs.capacity_, 0);
        buffer_ = std::exchange(rhs.buffer_, nullptr);
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    Vector() = default;

    explicit Vector(size_t size)
        : data_(size)
        , size_(size)  //
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other)
        : data_(other.size_)
        , size_(other.size_)  //
    {
        // Конструируем элементы в data_, копируя их из other.data_
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept {
        size_ = std::exchange(other.size_, 0);
        data_ = std::move(other.data_);
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            } else {

                if (size_ > rhs.Size()) {

                    for (size_t i = 0; i < rhs.Size(); ++i) {
                        data_[i] = rhs.data_[i];
                    }

                    std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
                    size_ = rhs.size_;

                } else {

                    for (size_t i = 0; i < size_; ++i) {
                        data_[i] = rhs.data_[i];
                    }

                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_, data_.GetAddress() + size_);
                    size_ = rhs.size_;
                }

            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            size_ = std::exchange(rhs.size_, 0);
            data_ = std::move(rhs.data_);
        }
        return *this;
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }

        RawMemory<T> new_data(new_capacity);
        UninitializedCopyOrMove(begin(), size_, new_data.GetAddress());
        std::destroy(begin(), end());
        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy(cbegin() + new_size, cend());

        } else if (new_size > size_) {
            Reserve(new_size);
            std::uninitialized_value_construct(begin() + size_, begin() + new_size);
        }

        size_ = new_size;
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *Emplace(cend(), std::forward<Args>(args)...);
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    void PopBack() /* noexcept */ {
        assert(size_ != 0);
        --size_;
        std::destroy_at(end());
    }

    iterator Erase(const_iterator pos) /*noexcept(std::is_nothrow_move_assignable_v<T>)*/ {
        assert(begin() <= pos && pos < end() && size_ != 0);
        iterator mutable_pos = begin() + (pos - cbegin());
        std::move(mutable_pos + 1, end(), mutable_pos);

        --size_;
        std::destroy_at(end());

        return mutable_pos;
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        assert(begin() <= pos && pos <= end());
        size_t new_item_offset = std::distance(cbegin(), pos);

        if (size_ == Capacity()) {
            EmplaceWithAllocation(pos, std::forward<Args>(args)...);
        } else {
            EmplaceWithoutAllocation(pos, std::forward<Args>(args)...);
        }

        ++size_;
        return begin() + new_item_offset;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    ~Vector() {
        std::destroy(begin(), end());
    }

    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator begin() const noexcept {
        return cbegin();
    }

    const_iterator end() const noexcept {
        return cend();
    }

    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator cend() const noexcept {
        return data_.GetAddress() + size_;
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    template <typename InputIt, typename OutputIt>
    void UninitializedCopyOrMove(InputIt first, size_t number_elements, OutputIt d_first) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(first, number_elements, d_first);
        } else {
            std::uninitialized_copy_n(first, number_elements, d_first);
        }
    }

    template <typename... Args>
    void EmplaceWithoutAllocation(const_iterator pos, Args&&... args) {
        if (pos == end()) {
            new (end()) T(std::forward<Args>(args)...);

        } else {
            new (end()) T(std::forward<T>(*(end() - 1)));
            iterator mutable_pos = begin() + (pos - cbegin());

            RawMemory<T> tmp(1);
            new (tmp.GetAddress()) T(std::forward<Args>(args)...);

            std::move_backward(mutable_pos, end() - 1, end());
            *mutable_pos = std::move(*tmp.GetAddress());
            std::destroy_at(tmp.GetAddress());
        }
    }

    template <typename... Args>
    void EmplaceWithAllocation(const_iterator pos, Args&&... args) {
        size_t new_item_offset = pos - cbegin();

        RawMemory<T> new_data((size_ == 0) ? 1 : size_ * 2);
        iterator new_items_pos = new_data.GetAddress() + new_item_offset;
        new (new_items_pos) T(std::forward<Args>(args)...);

        try {
            UninitializedCopyOrMove(begin(), new_item_offset, new_data.GetAddress());
        } catch (...) {
            std::destroy_at(new_items_pos);
            throw;
        }

        try {
            UninitializedCopyOrMove(begin() + new_item_offset, size_ - new_item_offset,
                                    new_data.GetAddress() + new_item_offset + 1);
        } catch (...) {
            std::destroy_n(new_data.GetAddress(), new_item_offset);
            throw;
        }

        std::destroy(begin(), end());
        data_.Swap(new_data);
    }
};
