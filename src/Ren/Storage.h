#pragma once

#include "HashMap32.h"
#include "SparseArray.h"

namespace Ren {
template<typename T>
class StorageRef;

template<typename T>
class Storage : public SparseArray<T> {
    HashMap32<String, uint32_t> items_by_name_;
public:
    template<class... Args>
    StorageRef<T> Add(Args &&... args) {
        uint32_t index = SparseArray<T>::emplace(args...);

        bool res = items_by_name_.Insert(SparseArray<T>::at(index).name(), index);
        assert(res);

        return { this, index };
    }

    void erase(uint32_t i) {
        const String &name = SparseArray<T>::at(i).name();

        bool res = items_by_name_.Erase(name);
        assert(res);

        SparseArray<T>::erase(i);
    }

    StorageRef<T> FindByName(const char *name) {
        uint32_t *p_index = items_by_name_.Find(name);
        if (p_index) {
            return { this, *p_index };
        } else {
            return { nullptr, 0 };
        }
    }
};

class RefCounter {
protected:
    template<class T> friend class StorageRef;

    void add_ref() {
        ++counter_;
    }
    bool release() {
        return --counter_ == 0;
    }

    RefCounter() : counter_(0) {}
    RefCounter(const RefCounter&) : counter_(0) {}
    RefCounter &operator=(const RefCounter&) {
        return *this;
    }
    //~RefCounter() {}

    //RefCounter(const RefCounter &) = delete;
    RefCounter(RefCounter &&rhs) : counter_(rhs.counter_) {
        rhs.counter_ = 0;
    }
    //RefCounter &operator=(const RefCounter &) = delete;
    RefCounter &operator=(RefCounter &&rhs) {
        counter_ = rhs.counter_;
        rhs.counter_ = 0;
        return *this;
    }
private:
    mutable unsigned counter_;
};

template <class T>
class StorageRef {
    Storage<T>  *storage_;
    uint32_t    index_;
public:
    StorageRef() : storage_(nullptr), index_(0) {}
    StorageRef(Storage<T> *storage, uint32_t index) : storage_(storage), index_(index) {
        if (storage_) {
            T &p = storage_->at(index_);
            p.add_ref();
        }
    }
    ~StorageRef() {
        Release();
    }

    StorageRef(const StorageRef &rhs) {
        storage_ = rhs.storage_;
        index_ = rhs.index_;

        if (storage_) {
            T &p = storage_->at(index_);
            p.add_ref();
        }
    }

    StorageRef(StorageRef &&rhs) {
        storage_ = rhs.storage_;
        rhs.storage_ = nullptr;
        index_ = rhs.index_;
        rhs.index_ = 0;
    }

    StorageRef &operator=(const StorageRef &rhs) {
        Release();

        storage_ = rhs.storage_;
        index_ = rhs.index_;

        if (storage_) {
            T &p = storage_->at(index_);
            p.add_ref();
        }

        return *this;
    }

    StorageRef &operator=(StorageRef &&rhs) {
        Release();

        storage_ = rhs.storage_;
        rhs.storage_ = nullptr;
        index_ = rhs.index_;
        rhs.index_ = 0;

        return *this;
    }

    T *operator->() {
        assert(storage_);
        return &storage_->at(index_);
    }

    const T *operator->() const {
        assert(storage_);
        return &storage_->at(index_);
    }

    T &operator*() {
        assert(storage_);
        return storage_->at(index_);
    }

    const T &operator*() const {
        assert(storage_);
        return storage_->at(index_);
    }

    T *get() {
        assert(storage_);
        return &storage_->at(index_);
    }

    const T *get() const {
        assert(storage_);
        return &storage_->at(index_);
    }

    operator bool() const {
        return storage_ != nullptr;
    }

    uint32_t index() const {
        return index_;
    }

    void Release() {
        if (storage_) {
            T *p = &storage_->at(index_);
            if (p->release()) {
                storage_->erase(index_);
            }
            storage_ = nullptr;
            index_ = 0;
        }
    }
};
}
