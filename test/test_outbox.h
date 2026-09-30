#ifndef TEST_OUTBOX_H
#define TEST_OUTBOX_H

/* An outbox with its own storage, as a hosting task's is, for tests that drive an actor directly.
 * Its capacity is the kind's largest burst, so a kind that sends more stops on Outbox_Next's
 * assert. */

#include <algorithm>
#include <cstdint>

extern "C" {
#include "outbox.h"
}

struct TestOutbox : Outbox {
    static constexpr uint8_t kMostAnyKindSends = 41U;
    Message storage[kMostAnyKindSends];

    explicit TestOutbox(uint8_t most = kMostAnyKindSends) : Outbox(), storage()
    {
        Outbox_Init(this, storage, most);
    }

    TestOutbox(const TestOutbox &other) : Outbox(other), storage()
    {
        std::copy(other.storage, other.storage + kMostAnyKindSends, storage);
        items = storage;
    }

    TestOutbox &operator=(const TestOutbox &other)
    {
        std::copy(other.storage, other.storage + kMostAnyKindSends, storage);
        capacity = other.capacity;
        count = other.count;
        items = storage;
        return *this;
    }
};

#endif /* TEST_OUTBOX_H */
