#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

bool TCPReceiver::segment_received(const TCPSegment &seg) {
    bool ret = false;
    static size_t abs_seqno = 0;
    size_t length;
    if (seg.header().syn) { // 处理 SYN 建立连接
        if (_syn_flag) {  
            return false;
        }
        _syn_flag = true;                       // 标记已成功建立连接
        ret = true;                             // SYN 包有效
        _isn = seg.header().seqno;              // 记录 ISN
        abs_seqno = 1;                          // SYN 占用序号 1
        _base = 1;                              // 下一个期望接收的绝对序号从 1 开始
        length = seg.length_in_sequence_space() - 1;    // 总长度减去 SYN 占用的 1 位
        if (length == 0) {                      
            return true;
        }
    } else if (!_syn_flag) {  // before get a SYN, refuse any segment
        return false;
    } else {  // not a SYN segment, compute it's abs_seqno
        abs_seqno = unwrap(WrappingInt32(seg.header().seqno.raw_value()), _isn, _base);
        length = seg.length_in_sequence_space();
    }

    if (seg.header().fin) { // 处理 FIN 连接终止
        if (_fin_flag) {  
            return false;
        }
        _fin_flag = true;
        ret = true;
    } else if (seg.length_in_sequence_space() == 0 && abs_seqno == _base) {
        return true;
    } else if (abs_seqno >= _base + window_size() || abs_seqno + length <= _base) { // 窗口判断
        if (!ret) return false;
    }

    _reassembler.push_substring(seg.payload().copy(), abs_seqno - 1, seg.header().fin); 
    _base = _reassembler.head_index() + 1;  // 更新下一个期望接收的序号 
    if (_reassembler.input_ended()) 
        _base++;    // FIN 占用一个序号
    return true;
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (_base > 0)
        return wrap(_base, _isn);
    else
        return std::nullopt;
}

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size(); } // 窗口大小 = 总容量 - 已缓存未读取的字节数
