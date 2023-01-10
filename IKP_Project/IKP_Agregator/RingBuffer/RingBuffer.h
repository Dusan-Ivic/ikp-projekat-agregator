#pragma once

#define RING_SIZE (16)

struct Message {
    int value;
    bool isImportant;
};

struct RingBuffer {
    unsigned int tail;
    unsigned int head;
    Message data[RING_SIZE];
};

Message getMessageFromBuffer(RingBuffer *buffer);

void addMessageToBuffer(RingBuffer *buffer, Message message);

bool isBufferEmpty(RingBuffer* buffer);

void getMessagesFromBuffer(RingBuffer *buffer, Message messages[], int* messageCount);
