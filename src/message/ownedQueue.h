#pragma once

#include <Arduino.h>

#include <memory>

/**
 * @brief FreeRTOS queue of owned objects.
 *
 * The queue stores raw pointers; ownership moves into the queue only when a send succeeds and
 * back out with receive(). Objects still queued when the queue is destroyed are deleted.
 */
template <typename T, typename Deleter = std::default_delete<T>>
class OwnedQueue {
public:
    using Ptr = std::unique_ptr<T, Deleter>;

    OwnedQueue() = default;
    ~OwnedQueue() { reset(); }
    OwnedQueue(const OwnedQueue&) = delete;
    OwnedQueue& operator=(const OwnedQueue&) = delete;

    /** Creates the queue with room for @p length objects. @return false if out of memory. */
    bool create(UBaseType_t length) {
        reset();
        handle_ = xQueueCreate(length, sizeof(T*));
        return handle_ != nullptr;
    }

    bool valid() const { return handle_ != nullptr; }

    /**
     * Queues @p item, waiting up to @p wait ticks for room.
     * @return true if the queue took @p item; false leaves @p item with the caller.
     */
    bool send(Ptr& item, TickType_t wait) {
        if (handle_ == nullptr || !item) return false;
        T* raw = item.get();
        if (xQueueSend(handle_, reinterpret_cast<const void*>(&raw), wait) != pdTRUE) return false;
        static_cast<void>(item.release());
        return true;
    }

    /** @return the oldest object, or nullptr if none arrives within @p wait ticks. */
    Ptr receive(TickType_t wait) {
        T* raw = nullptr;
        if (handle_ == nullptr ||
            xQueueReceive(handle_, reinterpret_cast<void*>(&raw), wait) != pdTRUE) {
            return Ptr();
        }
        return Ptr(raw);
    }

private:
    void reset() {
        if (handle_ == nullptr) return;
        while (receive(0)) {}
        vQueueDelete(handle_);
        handle_ = nullptr;
    }

    QueueHandle_t handle_ = nullptr;
};
