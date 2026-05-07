use std::sync::atomic::{AtomicUsize, Ordering};

pub struct CanFrame {
    pub id: u32,
    pub dlc: u8,
    pub data: [u8; 8],
    pub timestamp_ms: u32,
    pub extended: bool,
    pub rtr: bool,
}

pub struct RingBuffer {
    frames: Vec<CanFrame>,
    capacity: usize,
    head: AtomicUsize,
    tail: AtomicUsize,
}

impl RingBuffer {
    pub fn new(capacity: usize) -> Self {
        let mut frames = Vec::with_capacity(capacity);
        for _ in 0..capacity {
            frames.push(CanFrame {
                id: 0,
                dlc: 0,
                data: [0u8; 8],
                timestamp_ms: 0,
                extended: false,
                rtr: false,
            });
        }
        Self {
            frames,
            capacity,
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
        }
    }

    pub fn push(&self, frame: &CanFrame) -> bool {
        let head = self.head.load(Ordering::Relaxed);
        let next = (head + 1) % self.capacity;
        if next == self.tail.load(Ordering::Acquire) {
            return false; // full
        }
        unsafe {
            let slot = &self.frames[head] as *const CanFrame as *mut CanFrame;
            (*slot).id = frame.id;
            (*slot).dlc = frame.dlc;
            (*slot).data = frame.data;
            (*slot).timestamp_ms = frame.timestamp_ms;
            (*slot).extended = frame.extended;
            (*slot).rtr = frame.rtr;
        }
        self.head.store(next, Ordering::Release);
        true
    }

    pub fn pop(&self) -> Option<CanFrame> {
        let tail = self.tail.load(Ordering::Relaxed);
        if tail == self.head.load(Ordering::Acquire) {
            return None; // empty
        }
        let frame = &self.frames[tail];
        let result = CanFrame {
            id: frame.id,
            dlc: frame.dlc,
            data: frame.data,
            timestamp_ms: frame.timestamp_ms,
            extended: frame.extended,
            rtr: frame.rtr,
        };
        self.tail.store((tail + 1) % self.capacity, Ordering::Release);
        Some(result)
    }

    pub fn len(&self) -> usize {
        let head = self.head.load(Ordering::Relaxed);
        let tail = self.tail.load(Ordering::Relaxed);
        if head >= tail {
            head - tail
        } else {
            self.capacity - tail + head
        }
    }

    pub fn is_empty(&self) -> bool {
        self.head.load(Ordering::Relaxed) == self.tail.load(Ordering::Relaxed)
    }
}
