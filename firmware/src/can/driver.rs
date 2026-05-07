use anyhow::Result;
use esp_idf_hal::can;
use esp_idf_hal::delay::BLOCK;
use esp_idf_hal::gpio::{InputPin, OutputPin};

pub use can::config::{Mode, Timing};
pub use can::Frame;

pub struct CanBus<'d> {
    driver: can::CanDriver<'d>,
}

impl<'d> CanBus<'d> {
    pub fn open(
        can_peripheral: can::CAN<'d>,
        tx: impl OutputPin + 'd,
        rx: impl InputPin + 'd,
        timing: Timing,
        mode: Mode,
    ) -> Result<Self> {
        let filter = can::config::Filter::standard_allow_all();
        let config = can::config::Config::new()
            .timing(timing)
            .mode(mode)
            .filter(filter);

        let mut driver = can::CanDriver::new(can_peripheral, tx, rx, &config)?;
        driver.start()?;
        Ok(Self { driver })
    }

    pub fn transmit(&self, frame: &Frame) -> Result<()> {
        self.driver.transmit(frame, BLOCK)?;
        Ok(())
    }

    pub fn receive(&self) -> Result<Frame> {
        let frame = self.driver.receive(BLOCK)?;
        Ok(frame)
    }
}
