#include "RingBuffer.h"

Message getMessageFromBuffer(RingBuffer *buffer)
{
    int index;
    index = buffer->head;
    buffer->head = (buffer->head + 1) % RING_SIZE;
    return buffer->data[index];
}

void addMessageToBuffer(RingBuffer *buffer, Message message)
{
    buffer->data[buffer->tail] = message;
    buffer->tail = (buffer->tail + 1) % RING_SIZE;
}

bool isBufferEmpty(RingBuffer* buffer) {
    return buffer->head == buffer->tail;
}
