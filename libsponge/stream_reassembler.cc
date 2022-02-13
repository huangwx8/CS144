#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void StreamReassembler::write_output() {
    string data;
    for (size_t c = 0; c < _output.remaining_capacity() && _bitmap[0]; c++) {
        data += _buf[0];
        _bitmap.pop_front();
        _bitmap.push_back(false);
        _buf.pop_front();
        _buf.push_back(0);
        _unassembled_bytes--;
        _offset++;
    }
    if (data.size() > 0)
        _output.write(data);
    if (_eof && empty())
        _output.end_input();
}

StreamReassembler::StreamReassembler(const size_t capacity)
    : _buf(capacity, 0)
    , _bitmap(capacity, false)
    , _unassembled_bytes(0)
    , _offset(0)
    , _eof(false)
    , _output(capacity)
    , _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    size_t data_index;
    size_t buf_index;
    if (index >= _offset) {
        data_index = 0;
        buf_index = index - _offset;
    } else {
        data_index = _offset - index;
        buf_index = 0;
    }

    for (; data_index < data.size() && buf_index < _buf.size(); data_index++, buf_index++) {
        _buf[buf_index] = data[data_index];
        if (_bitmap[buf_index] == false)
            _unassembled_bytes++;
        _bitmap[buf_index] = true;
    }

    if (eof)
        _eof = true;

    write_output();
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes; }

bool StreamReassembler::empty() const { return _unassembled_bytes == 0; }
