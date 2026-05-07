use super::buffer::CanFrame;

#[derive(Debug)]
pub enum HondaParam {
    Rpm(u16),
    Speed(u16),
    ThrottlePosition(u8),
    CoolantTemp(i16),
    GearPosition(u8),
    FuelLevel(u8),
    Unknown(u32, [u8; 8]),
}

pub fn decode_honda_frame(frame: &CanFrame) -> HondaParam {
    // CAN IDs must be reverse-engineered from actual ADV350 captures.
    // These are placeholder IDs — replace after Phase 5 capture sessions.
    HondaParam::Unknown(frame.id, frame.data)
}

pub fn format_param(param: &HondaParam) -> String {
    match param {
        HondaParam::Rpm(v) => format!("RPM: {}", v),
        HondaParam::Speed(v) => format!("Speed: {} km/h", v),
        HondaParam::ThrottlePosition(v) => format!("Throttle: {}%", v),
        HondaParam::CoolantTemp(v) => format!("Coolant: {}°C", v),
        HondaParam::GearPosition(v) => format!("Gear: {}", v),
        HondaParam::FuelLevel(v) => format!("Fuel: {}%", v),
        HondaParam::Unknown(id, data) => {
            format!("ID:0x{:03X} [{}]", id, data.iter().map(|b| format!("{:02X}", b)).collect::<Vec<_>>().join(" "))
        }
    }
}
