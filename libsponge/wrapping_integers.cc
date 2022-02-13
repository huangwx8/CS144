#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    uint32_t start = isn.raw_value();
    uint64_t seqno = (n + start) & UINT32_MAX;
    return WrappingInt32{static_cast<uint32_t>(seqno)};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    uint32_t x = n.raw_value() - isn.raw_value();
    uint64_t high32 = checkpoint >> 32 << 32;
    uint64_t low32 = checkpoint << 32 >> 32;
    uint64_t closest;

    if (x > low32) {
        if (x - low32 > (1ul << 31) && high32 > 0) {
            closest = (high32 - (1ul << 32)) | x;
        } else {
            closest = high32 | x;
        }
    } else {
        if (low32 - x > (1ul << 31)) {
            closest = (high32 + (1ul << 32)) | x;
        } else {
            closest = high32 | x;
        }
    }

    return closest;
}
