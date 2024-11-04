#pragma once

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

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
        buffer_ = std::exchange(other.buffer_, nullptr);
        capacity_ = std::exchange(other.capacity_,0);
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept {
        if(this != &rhs) {
            Deallocate(buffer_);
            buffer_ = std::exchange(rhs.buffer_, nullptr);
            capacity_ = std::exchange(rhs.capacity_,0);
        }
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
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
    , size_(other.size_)
    {
        std::uninitialized_copy_n(other.data_.GetAddress(),other.size_,data_.GetAddress());
    }

    Vector(Vector&& other) noexcept
    : data_{}
    , size_{0}
    {
        Swap(other);
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            } else {
                /* Скопировать элементы из rhs, создав при необходимости новые
                 *                 или удалив существующие */
                if (size_ > rhs.size_) {
                    std::copy(rhs.data_.GetAddress(),rhs.data_.GetAddress() + rhs.size_,data_.GetAddress());
                    std::destroy_n(data_.GetAddress()+ rhs.size_, size_ - rhs.size_);
                } else {
                    std::copy(rhs.data_.GetAddress(), rhs.data_.GetAddress() + size_, data_.GetAddress());
                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_,data_.GetAddress()+ size_);
                }
            }
            size_ = rhs.size_;
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }

    ~Vector() {
        DestroyN(data_.GetAddress(),size_);
    }

    iterator begin() noexcept {
        return data_.GetAddress();
    }
    iterator end() noexcept {
        return (data_.GetAddress() + size_);
    }
    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }
    const_iterator end() const noexcept {
        return (data_.GetAddress() + size_);
    }
    const_iterator cbegin() const noexcept {
        return begin();
    }
    const_iterator cend() const noexcept {
        return end();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        iterator nc_pos = const_cast<iterator>(pos);
        if(size_ < Capacity()) {
            if(pos == end()) {
                new (end()) T(std::forward<Args>(args)...);
                nc_pos = end();
            } else {
                T tmp(std::forward<Args>(args)...);
                std::uninitialized_move_n(end()-1,1,end());
                std::move_backward(nc_pos,end() - 1, end());
                *nc_pos = std::move(tmp);
            }
            ++size_;
            return nc_pos;
        }

        size_t size = 0;
        if(data_.Capacity() == 0) {
            size = 1;
        } else {
            size = data_.Capacity() * 2;
        }
        RawMemory<T> new_data(size);
        T* new_data_pos = new_data.GetAddress() + (nc_pos - data_.GetAddress());
        new (new_data_pos) T(std::forward<Args>(args)...);
        if(begin() != end()) {
            try {
                CopyOrMoveData(data_.GetAddress(), nc_pos - data_.GetAddress(), new_data.GetAddress());
            } catch(...) {
                new_data_pos->~T();
            }
        }

        try {
            CopyOrMoveData(nc_pos, end() - nc_pos, new_data_pos + 1);
        } catch(...) {
            DestroyN(new_data.GetAddress(),new_data_pos - new_data.GetAddress());
        }

        data_.Swap(new_data);
        DestroyN(new_data.GetAddress(), size_);
        ++size_;
        return new_data_pos;
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *Emplace(end(),std::forward<Args>(args)...);
    }

    iterator Erase(const_iterator pos) {
        iterator nc_pos = const_cast<iterator>(pos);
        std::move(nc_pos+1, end(), nc_pos);
        Destroy(end()-1);
        --size_;
        return nc_pos;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }
    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    void PopBack() noexcept {
        Destroy(end() - 1);
        --size_;
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }

        RawMemory<T> new_data(new_capacity);
        CopyOrMoveData(data_.GetAddress(), size_, new_data.GetAddress());
        new_data.Swap(data_);
        std::destroy_n(new_data.GetAddress(), size_);
    }

    void Resize(size_t new_size) {
        if(new_size < size_) {
            DestroyN(data_.GetAddress() + new_size, size_ - new_size);
            size_ = new_size;
        } else if(new_size > size_) {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
            size_ = new_size;
        }
    }

    size_t Size() const noexcept {
        return size_;
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_,other.size_);
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    static void DestroyN(T* buf, size_t n) noexcept {
        for (size_t i = 0; i != n; ++i) {
            Destroy(buf + i);
        }
    }

    void CopyOrMoveData(T* begin, size_t size, T* end) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(begin, size, end);
        } else {
            std::uninitialized_copy_n(begin, size, end);
        }
    }

    // Создаёт копию объекта elem в сырой памяти по адресу buf
    static void CopyConstruct(T* buf, const T& elem) {
        new (buf) T(elem);
    }

    // Вызывает деструктор объекта по адресу buf
    static void Destroy(T* buf) noexcept {
        buf->~T();
    }
};
