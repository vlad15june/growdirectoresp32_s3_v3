#include "FreeRtosMessageQueue.h"

// FreeRtosMessageQueue is a template, so nothing in the header is compiled until it is
// instantiated. Nothing instantiates it yet - the first user is the send-off-the-loop change -
// and an uncompiled header is an unchecked header. This explicit instantiation makes the
// device build type-check it: void* stands for the by-pointer payload the wrapper is meant
// to carry.
template class FreeRtosMessageQueue<void*>;
