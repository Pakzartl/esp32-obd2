use crate::can::buffer::CanFrame;

const HEX_LUT: &[u8; 16] = b"0123456789ABCDEF";

fn hex_char(nibble: u8) -> u8 {
    HEX_LUT[(nibble & 0x0F) as usize]
}

fn from_hex_char(c: u8) -> Option<u8> {
    match c {
        b'0'..=b'9' => Some(c - b'0'),
        b'A'..=b'F' => Some(c - b'A' + 10),
        b'a'..=b'f' => Some(c - b'a' + 10),
        _ => None,
    }
}

/// Encode a CAN frame to SLCAN ASCII format.
/// Standard frame: tIIILDD..DD\r
/// Extended frame: TIIIIIIIILDD..DD\r
pub fn encode_frame(frame: &CanFrame) -> Vec<u8> {
    let mut buf = Vec::with_capacity(32);

    if frame.rtr {
        buf.push(if frame.extended { b'R' } else { b'r' });
    } else {
        buf.push(if frame.extended { b'T' } else { b't' });
    }

    if frame.extended {
        for shift in (0..8).rev() {
            buf.push(hex_char(((frame.id >> (shift * 4)) & 0xF) as u8));
        }
    } else {
        buf.push(hex_char(((frame.id >> 8) & 0xF) as u8));
        buf.push(hex_char(((frame.id >> 4) & 0xF) as u8));
        buf.push(hex_char((frame.id & 0xF) as u8));
    }

    buf.push(hex_char(frame.dlc));

    if !frame.rtr {
        for i in 0..frame.dlc as usize {
            buf.push(hex_char(frame.data[i] >> 4));
            buf.push(hex_char(frame.data[i]));
        }
    }

    buf.push(b'\r');
    buf
}

/// Decode an SLCAN ASCII command into a CAN frame.
pub fn decode_frame(cmd: &[u8]) -> Option<CanFrame> {
    if cmd.is_empty() {
        return None;
    }

    let (extended, rtr) = match cmd[0] {
        b't' => (false, false),
        b'T' => (true, false),
        b'r' => (false, true),
        b'R' => (true, true),
        _ => return None,
    };

    let (id, data_start) = if extended {
        if cmd.len() < 10 {
            return None;
        }
        let mut id: u32 = 0;
        for i in 1..9 {
            id = (id << 4) | from_hex_char(cmd[i])? as u32;
        }
        (id, 9)
    } else {
        if cmd.len() < 5 {
            return None;
        }
        let id = ((from_hex_char(cmd[1])? as u32) << 8)
            | ((from_hex_char(cmd[2])? as u32) << 4)
            | (from_hex_char(cmd[3])? as u32);
        (id, 4)
    };

    let dlc = from_hex_char(cmd[data_start])?;
    if dlc > 8 {
        return None;
    }

    let mut data = [0u8; 8];
    if !rtr {
        let expected_len = data_start + 1 + (dlc as usize * 2);
        if cmd.len() < expected_len {
            return None;
        }
        for i in 0..dlc as usize {
            let offset = data_start + 1 + (i * 2);
            data[i] = (from_hex_char(cmd[offset])? << 4) | from_hex_char(cmd[offset + 1])?;
        }
    }

    Some(CanFrame {
        id,
        dlc,
        data,
        timestamp_ms: 0,
        extended,
        rtr,
    })
}

#[derive(Debug, PartialEq)]
pub enum SlcanBitrate {
    B10K,
    B20K,
    B50K,
    B100K,
    B125K,
    B250K,
    B500K,
    B800K,
    B1M,
}

pub fn parse_bitrate(code: u8) -> Option<SlcanBitrate> {
    match code {
        b'0' => Some(SlcanBitrate::B10K),
        b'1' => Some(SlcanBitrate::B20K),
        b'2' => Some(SlcanBitrate::B50K),
        b'3' => Some(SlcanBitrate::B100K),
        b'4' => Some(SlcanBitrate::B125K),
        b'5' => Some(SlcanBitrate::B250K),
        b'6' => Some(SlcanBitrate::B500K),
        b'7' => Some(SlcanBitrate::B800K),
        b'8' => Some(SlcanBitrate::B1M),
        _ => None,
    }
}
