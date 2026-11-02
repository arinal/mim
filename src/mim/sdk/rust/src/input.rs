// SPDX-License-Identifier: MIT OR GPL-2.0-only

//! The remote: which keys went down or up since the last frame. The host drops the OS's key
//! repeat, so an app that wants a held key to repeat times it itself.

use crate::ffi;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Key {
    Left,
    Right,
    Up,
    Down,
    Ok,
    Back,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Event {
    pub key: Key,
    /// true: it went down; false: it came back up
    pub pressed: bool,
}

/// The next of this frame's events, oldest first; each comes once. Skips any event whose kind or
/// key this SDK doesn't know, so a newer host can add more.
pub fn next_event() -> Option<Event> {
    loop {
        // a kind and a key, as the host writes them
        let mut raw = [0u32; ffi::EVENT_SIZE / 4];
        // the buffer is ours and holds one event, so the host's check passes
        if unsafe { ffi::read_events(raw.as_mut_ptr().cast(), 1) } == 0 {
            return None;
        }
        let pressed = match raw[0] as usize {
            ffi::EVENT_PRESS => true,
            ffi::EVENT_RELEASE => false,
            _ => continue,
        };
        let key = match raw[1] as usize {
            ffi::KEY_LEFT => Key::Left,
            ffi::KEY_RIGHT => Key::Right,
            ffi::KEY_UP => Key::Up,
            ffi::KEY_DOWN => Key::Down,
            ffi::KEY_OK => Key::Ok,
            ffi::KEY_BACK => Key::Back,
            _ => continue,
        };
        return Some(Event { key, pressed });
    }
}
