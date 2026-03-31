#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

template<typename T>
class ThreadSafeQueue {
private:
    std::queue<T> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    bool closed_ = false;

public:
    ThreadSafeQueue() = default;

    // No copy
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    // Push (ritorna false se la queue è chiusa)
    bool push(const T& value) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (closed_) return false;
            queue_.push(value);
        }
        cv_.notify_one();
        return true;
    }

    bool push(T&& value) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (closed_) return false;
            queue_.push(std::move(value));
        }
        cv_.notify_one();
        return true;
    }

    // Pop bloccante
    // ritorna std::nullopt se la coda è chiusa e vuota
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mtx_);

        cv_.wait(lock, [this] {
            return closed_ || !queue_.empty();
        });

        if (queue_.empty()) {
            // chiusa e vuota
            return std::nullopt;
        }

        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    // Pop non bloccante
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mtx_);

        if (queue_.empty())
            return std::nullopt;

        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    // Check non bloccante
    bool empty() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.empty();
    }

    // Chiusura della coda
    void close() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    // Stato
    bool isClosed() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return closed_;
    }
};
