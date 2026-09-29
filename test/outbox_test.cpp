/* The outbox: what one message sends, in storage its owner provides. */

#include <gtest/gtest.h>

#include <cstring>

extern "C" {
#include "outbox.h"
}

TEST(OutboxTest, should_start_empty_whatever_its_storage_and_memory_held)
{
    Message storage[2];
    Outbox outbox;
    std::memset(&outbox, 0xFF, sizeof(outbox));
    Outbox_Init(&outbox, storage, 2U);
    EXPECT_EQ(0U, outbox.count);
    EXPECT_EQ(2U, outbox.capacity);
    EXPECT_EQ(&storage[0], Outbox_Next(&outbox, MSG_REPLY, 1U, 2U));
}
