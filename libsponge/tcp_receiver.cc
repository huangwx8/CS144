#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    uint64_t index = 0;

    if (!_isn.has_value()) {     // LISTEN
        if (seg.header().syn) {  // LISTEN -> SYN_RECV
            _isn = seg.header().seqno;
            index = 0;
        } else {
            // error();
        }
    } else if (_isn.has_value() && !stream_out().input_ended()) {  // SYN_RECV
        index = unwrap(seg.header().seqno, _isn.value(), _last_reassembled) - 1;
    } else if (stream_out().input_ended()) {  // FIN_RECV
        index = unwrap(seg.header().seqno, _isn.value(), _last_reassembled) - 1;
    } else {
        // error();
    }

    if (_isn.has_value()) {
        _reassembler.push_substring(seg.payload().copy(), index, seg.header().fin);
        _last_reassembled = index;
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_isn.has_value()) {  // LISTEN
        return {};
    }
    if (_isn.has_value() && !stream_out().input_ended()) {  // SYN_RECV
        return wrap(stream_out().bytes_written() + 1, _isn.value());
    } else if (stream_out().input_ended()) {  // FIN_RECV
        return wrap(stream_out().bytes_written() + 2, _isn.value());
    } else {
        // error();
    }
    return {};
}

size_t TCPReceiver::window_size() const { return _capacity - stream_out().buffer_size(); }
