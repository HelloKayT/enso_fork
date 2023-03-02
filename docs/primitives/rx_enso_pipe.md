# RX Ensō Pipe

RX Ensō Pipes are used to *receive* data from the NIC. They offer multiple options for receiving data, including [byte streams](#receiving-byte-streams), [raw packets](#receiving-raw-packets), or [application-level messages](#receiving-generic-messages). The appropriate option to use depends on the level of abstraction supported by the NIC and required by the application.


## Receiving byte streams

The most generic way of receiving data in an RX Ensō Pipe is to use [`RxPipe::Recv()`](/software/classnorman_1_1RxPipe.html#a9d32b67355ca10fa5897a3292a224ed2){target=_blank}. It will return the next chunk of bytes available in the pipe.

After calling `RxPipe::Recv()`, the application will own the data and is responsible for freeing it once it is done processing. To do so, the application should call [`RxPipe::Free()`](/software/classnorman_1_1RxPipe.html#a8fa3b56991665b6e65a22cd145a62be0){target=_blank} or [`RxPipe::Clear()`](/software/classnorman_1_1RxPipe.html#a231fee8c8088b9d8e6f92977a0e8538a){target=_blank}. The difference between the two is that `RxPipe::Free()` takes as argument the number of bytes to free, while `RxPipe::Clear()` frees all the data currently owned by the application. Note that received data can only be freed sequentially.

The following example shows how to use `RxPipe::Recv()` and `RxPipe::Clear()` to receive, process, and free data.

```cpp
RxPipe* rx_pipe = device->AllocateRxPipe(); // (1)!
assert(rx_pipe != nullptr);

// This is arbitrary and can be tuned to the application's needs.
constexpr uint32_t kMaxBatchSize = 65536;

uint8_t* buf;

uint32_t nb_bytes_received = rx_pipe->Recv(&buf, kMaxBatchSize);

// Do something with the received data.
// [...]

// Freeing all the received data.
rx_pipe->Clear();
```

1. :information_source: Note that you should use a [Device](device.md) instance to allocate an RX Ensō Pipe.

### Accumulating data

Applications do not need to process all the data received at once. Instead, they may choose to accumulate data, calling `RxPipe::Recv()` multiple times before finally freeing it. At any point, the application can check the number of bytes that it currently owns by calling [`RxPipe::capacity()`](/software/classnorman_1_1RxPipe.html#ac5e519460cd70167cdc6e4c41123ce17){target=_blank}.

While applications are free to accumulate data, they should be careful not to accumulate too much. If the application does not free the data, it will eventually own the entire RX Ensō Pipe's buffer. This will prevent new data from being received from the NIC.

!!! note

    Applications cannot own more data than the RX Ensō Pipe's overall capacity ([`RxPipe::kMaxCapacity`](/software/classnorman_1_1RxPipe.html#a552209a1f30e60a501bf8467a26e8d45){target=_blank}). As such, if `RxPipe::capacity()` is equal to `RxPipe::kMaxCapacity`, calling `RxPipe::Recv()` will always return 0. As a rule of thumb, try to prevent `RxPipe::capacity()` from exceeding `RxPipe::kMaxCapacity / 2`.

### Peeking

Sometimes, it is useful to be able to peek at the data without actually consuming it.[^1] This can be accomplished by using [`RxPipe::Peek()`](/software/classnorman_1_1RxPipe.html#ac858c31850556a98a54ff0097d53a1aa){target=_blank}. `RxPipe::Peek()` works similarly to `RxPipe::Recv()`, except that it does not consume the data from the pipe. As such, a later call to `RxPipe::Peek()` or `RxPipe::Recv()` will return the same data. If desired, the application can call [`RxPipe::ConfirmBytes()`](/software/classnorman_1_1RxPipe.html#a0f03b6e436d5b1332fcf682d1ffdb205){target=_blank} to explicitly consume the data after peeking.

[^1]: This is analogous to the `MSG_PEEK` flag in the `recv(2)` system call.


## Receiving raw packets

While `RxPipe::Recv()` can be used to receive generic data, RX Ensō Pipes also support a more convenient way of receiving raw packets. The [`RxPipe::RecvPkts()`](/software/classnorman_1_1RxPipe.html#a634355efbf20a3b1cf96d252e9fdc90c){target=_blank} method returns a batch of packets that can be iterated over using a range-based for loop. The following example shows how to use `RxPipe::RecvPkts()` to receive and process packets.

```cpp
auto batch = rx_pipe->RecvPkts();
for (auto pkt : batch) {
  // Do something with the packet.
  // [...]
}

rx_pipe->Clear();
```

In the example above, there is no limit to the batch of packets returned by `RxPipe::RecvPkts()`. If desired, you may also set a maximum batch size (in number of packets) when calling `RxPipe::RecvPkts()`. After iterating over a batch, you can retrieve the total number of bytes in the batch by calling [`RxPipe::MessageBatch::processed_bytes()`](/software/classnorman_1_1RxPipe_1_1MessageBatch.html#aa62d293fd306570db12372c20bcacf51){target=_blank}. For example:

```cpp
RxPipe* rx_pipe = device->AllocateRxPipe();
assert(rx_pipe != nullptr);

// This is arbitrary and can be tuned to the application's needs.
constexpr uint32_t kMaxPktBatchSize = 1024;

auto batch = rx_pipe->RecvPkts(kMaxPktBatchSize);

// Should print "Processed bytes: 0".
std::cout << "Processed bytes: " << batch.processed_bytes() << std::endl;

for (auto pkt : batch) {
  // Do something with the packet.
  // [...]
}

// Will show the total number of bytes processed in the batch.
std::cout << "Processed bytes: " << batch.processed_bytes() << std::endl;

rx_pipe->Clear();
```

In addition to `RxPipe::RecvPkts()`, RX Ensō Pipes also support peeking packets using [`RxPipe::PeekPkts()`](/software/classnorman_1_1RxPipe.html#a89892a72350394b23b94b70da8fd7eb6){target=_blank}. Similar to `RxPipe::Peek()`, `RxPipe::PeekPkts()` does not consume the data from the pipe.

## Receiving generic messages

The third way of receiving data is by using [`RxPipe::RecvMessages()`](/software/classnorman_1_1RxPipe.html#aa8a89162aa4549660433fcb7b2ca2b99){target=_blank}. `RxPipe::RecvMessages()` allows the application to use its own message format. In fact, `RxPipe::RecvPkts()` and `RxPipe::PeekPkts()` are just special cases of `RxPipe::RecvMessages()` for raw packets.

To use `RxPipe::RecvMessages()` applications must supply an implementation of a message iterator. To do so, define a class that inherits from [`MessageIteratorBase`](/software/classnorman_1_1MessageIteratorBase.html){target=_blank}. This class should implement two methods: `GetNextMessage()` and `OnAdvanceMessage()`:

- `GetNextMessage()` takes as argument the current message in the batch. It should use this message to return the address of the next message.
- `OnAdvanceMessage()` takes as argument the number of bytes from the last message and is called whenever the application has finished processing a message. It can be used, for instance, to call `ConfirmBytes()` on the pipe.

The following example shows the implementation of the message iterator for raw packets.

```cpp
#include <norman/helpers.h>
#include <norman/pipe.h>

namespace norman {

class PktIterator : public MessageIteratorBase<PktIterator> {
 public:
  inline PktIterator(uint8_t* addr, int32_t message_limit,
                     RxPipe::MessageBatch<PktIterator>* batch)
      : MessageIteratorBase(addr, message_limit, batch) {} // (1)!

  constexpr inline uint8_t* GetNextMessage(uint8_t* current_message) {
    return get_next_pkt(current_message); // (2)!
  }

  constexpr inline void OnAdvanceMessage(uint32_t nb_bytes) {
    batch_->pipe_->ConfirmBytes(nb_bytes); // (3)!
  }
};

}  // namespace norman
```

1. The constructor takes as arguments the address of the first message in the batch, the maximum number of messages to process, and a pointer to the batch. The constructor of the base class must be called with these arguments.
2. We are simply using the [`get_next_pkt()`](/software/helpers_8h.html#a3503cddb67d77f960d28fee7c739500a){target=_blank} helper function to get the address of the next packet.
3. You can have access to any member of the batch object. Here, we are calling `ConfirmBytes()` on the pipe to consume the data. `PeekPktIterator` simply suppresses this call.

Once you have defined your message iterator, you can then use it to receive messages using `RxPipe::RecvMessages()` similarly to how you would use `RxPipe::RecvPkts()`.

```cpp
auto batch = rx_pipe->RecvMessages<PktIterator>();

for (auto message : batch) {
  // Do something with the packet.
  // [...]
}

rx_pipe->Clear();
```

## Binding and flow steering

The NIC is responsible for demultiplexing incoming data among the RX Ensō Pipes. The logic to demultiplex packets should depend on the offloads implemented on the NIC. For convenience, our hardware implementation includes two types of flow steering mechanisms: flow hashing and flow binding.

Flow binding is implemented using a cuckoo hash table. This allows the application to map arbitrary flows to RX Ensō Pipes. We borrow from the socket API terminology and call this mapping between RX Ensō Pipes and flows *binding*. To bind an RX Ensō Pipe to a flow, you can use [`RxPipe::Bind()`](/software/classnorman_1_1RxPipe.html#ab443243a6e7f473853f72eb6926f635c), specifying the flow's five-tuple. You can call `RxPipe::Bind()` multiple times on the same pipe to bind it to multiple flows.

Packets that do not match any flow in the flow table are directed to fallback queues using a hash of the packet's 5-tuple. The number of fallback queues should be configured in the hardware. If the number of fallback queues is set to *n*, the first *n* RX Ensō Pipes are used as fallback queues.

<!-- TODO(sadok): Should describe how to configure the hardware. -->


## Examples

The following examples use RX Ensō Pipes:

- [`new_capture.cpp`](https://github.com/hsadok/norman/blob/master/software/examples/new_capture.cpp){target=_blank}
- [`new_echo_copy.cpp`](https://github.com/hsadok/norman/blob/master/software/examples/new_echo_copy.cpp){target=_blank}


## Summary

- Use `RxPipe::Recv()` to receive arbitrary data from an RX Ensō Pipe and `RxPipe::Peek()` to peek at the data without consuming it.
- Use `RxPipe::RecvPkts()` to receive raw packets from an RX Ensō Pipe and `RxPipe::PeekPkts()` to peek at the packets without consuming them.
- Use `RxPipe::RecvMessages()` to receive messages from an RX Ensō Pipe. You must provide a message iterator to use this method.
- Use `RxPipe::Clear()` or `RxPipe::Free()` to free data after you are done processing it.
- The number of bytes currently owned by the application can be obtained using `RxPipe::capacity()`.
- Use `RxPipe::Bind()` to bind an RX Ensō Pipe to a flow.