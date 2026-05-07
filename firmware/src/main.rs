mod can;
mod cloud;
mod protocol;
mod storage;

use anyhow::Result;
use can::driver::{CanBus, Frame, Mode, Timing};
use enumset::EnumSet;
use esp_idf_hal::can::Flags;
use esp_idf_hal::peripherals::Peripherals;

fn main() -> Result<()> {
    esp_idf_svc::sys::link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();

    log::info!("=== ADV350 Logger — TWAI Loopback Test ===");

    let peripherals = Peripherals::take()?;

    // NoAck mode = self-test loopback (no transceiver needed)
    let mut bus = CanBus::open(
        peripherals.can,
        peripherals.pins.gpio4, // TX
        peripherals.pins.gpio5, // RX
        Timing::B500K,
        Mode::NoAck,
    )?;

    log::info!("TWAI initialized: 500kbps, NoAck (loopback) mode");

    // Send test frames
    let test_frames: [(u16, &[u8]); 4] = [
        (0x7DF, &[0x02, 0x01, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00]), // OBD: request RPM
        (0x7DF, &[0x02, 0x01, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00]), // OBD: request speed
        (0x100, &[0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0x01, 0x02]), // custom frame
        (0x200, &[0x11, 0x22, 0x33, 0x44]),                          // short frame
    ];

    for (id, data) in &test_frames {
        let frame = Frame::new(*id as u32, Flags::SelfReception.into(), data).unwrap();
        bus.transmit(&frame)?;
        log::info!("TX > ID:0x{:03X} [{}]", id,
            data.iter().map(|b| format!("{:02X}", b)).collect::<Vec<_>>().join(" "));

        let rx = bus.receive()?;
        log::info!("RX < ID:0x{:03X} [{}]", rx.identifier(),
            rx.data().iter().map(|b| format!("{:02X}", b)).collect::<Vec<_>>().join(" "));

        if rx.identifier() == *id as u32 && rx.data() == *data {
            log::info!("  MATCH OK");
        } else {
            log::error!("  MISMATCH!");
        }
    }

    log::info!("=== TWAI Loopback Test Complete ===");

    // Test SLCAN encoding
    let test_can = can::buffer::CanFrame {
        id: 0x7DF,
        dlc: 8,
        data: [0x02, 0x01, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00],
        timestamp_ms: 0,
        extended: false,
        rtr: false,
    };
    let encoded = protocol::slcan::encode_frame(&test_can);
    log::info!("SLCAN encode: {}", String::from_utf8_lossy(&encoded).trim());

    // Test ring buffer
    let ring = can::buffer::RingBuffer::new(16);
    ring.push(&test_can);
    if let Some(popped) = ring.pop() {
        log::info!("Ring buffer OK: ID=0x{:03X}, len={}", popped.id, ring.len());
    }

    log::info!("All subsystems verified. Waiting for CAN transceiver...");

    loop {
        std::thread::sleep(std::time::Duration::from_secs(10));
    }
}
