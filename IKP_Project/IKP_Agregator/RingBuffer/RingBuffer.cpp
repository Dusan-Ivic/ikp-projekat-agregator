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

void getMessagesFromBuffer(RingBuffer *buffer, Message messages[], int* messageCount)
{
    int index = 0;
    while (!isBufferEmpty(buffer))
    {
        Message message = getMessageFromBuffer(buffer);
        if (message.value > 0)
        {
            messages[index] = message;
            (*messageCount)++;
            index++;
        }
    }
}