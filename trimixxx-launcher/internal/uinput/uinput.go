//go:build linux

// Package uinput creates virtual input devices in the kernel.
//
// This is what makes the deck able to drive software that has never heard of
// MIDI. A uinput device is indistinguishable from a real USB keyboard or mouse
// from userspace up: udev sees a hotplug, X/libinput adds it as an input device,
// SDL reads it as a keyboard. So Doom needs no patching, no fork and no MIDI
// support -- it just gets keypresses -- and the same device serves a bare
// console, which is the only reason a rescue screen on this unit is usable at
// all (the deck has no keyboard port and nothing to plug into it).
//
// Requires root (or write access to /dev/uinput), which the launch manager
// already has.
package uinput

import (
	"fmt"
	"os"
	"syscall"
	"time"
	"unsafe"
)

// Everything below is transcribed from <linux/uinput.h> and <linux/input.h>.
// The ioctl numbers are _IOW('U', n, int) = 0x40 << 24 | sizeof(int) << 16 |
// 'U' << 8 | n, precomputed so this package needs no cgo -- which matters,
// because the ship build is a static musl cross-compile.
const (
	devPath     = "/dev/uinput"
	maxNameSize = 80
	absCount    = 64

	uiDevCreate  = 0x5501     // _IO('U', 1)
	uiDevDestroy = 0x5502     // _IO('U', 2)
	uiSetEvBit   = 0x40045564 // _IOW('U', 100, int)
	uiSetKeyBit  = 0x40045565 // _IOW('U', 101, int)
	uiSetRelBit  = 0x40045566 // _IOW('U', 102, int)

	evSyn = 0x00
	evKey = 0x01
	evRel = 0x02

	synReport = 0x00

	busVirtual = 0x06
)

// Relative axes. Only X is actually used (the jog wheel is a turn axis), but a
// pointer device that reports only one axis is a strange animal to libinput, so
// both are declared.
const (
	RelX = 0x00
	RelY = 0x01
)

// Mouse buttons, declared so libinput classifies the pointer device as a mouse.
const (
	BtnLeft   = 0x110
	BtnRight  = 0x111
	BtnMiddle = 0x112
)

// settle is how long to wait after creating a device before writing to it.
// udev has to notice the hotplug and X/libinput has to open it; events emitted
// in that gap are simply dropped on the floor. This is the standard uinput
// caveat and the usual cause of "the first keypress never arrives".
const settle = 300 * time.Millisecond

// inputEvent is struct input_event. syscall.Timeval is per-architecture, which
// is exactly what is needed here: the kernel's struct is 24 bytes on arm64 and
// 16 on 32-bit ARM, and this mirrors that automatically.
type inputEvent struct {
	Time  syscall.Timeval
	Type  uint16
	Code  uint16
	Value int32
}

type inputID struct{ BusType, Vendor, Product, Version uint16 }

// userDev is struct uinput_user_dev, written to the fd before UI_DEV_CREATE.
// (The newer UI_DEV_SETUP ioctl does the same job; this legacy path is one
// write and works on every kernel that has uinput at all.)
type userDev struct {
	Name         [maxNameSize]byte
	ID           inputID
	FFEffectsMax uint32
	AbsMax       [absCount]int32
	AbsMin       [absCount]int32
	AbsFuzz      [absCount]int32
	AbsFlat      [absCount]int32
}

// Device is one virtual input device.
type Device struct {
	f    *os.File
	name string
}

// New creates a device advertising the given key codes and relative axes. A
// device may only ever emit what it declared here, so the key list has to be
// the full union of everything a keymap can produce -- a key not declared is
// silently discarded by the kernel, which looks exactly like a broken mapping.
func New(name string, keys []uint16, rels []uint16) (*Device, error) {
	f, err := os.OpenFile(devPath, os.O_WRONLY|syscall.O_NONBLOCK, 0)
	if err != nil {
		return nil, fmt.Errorf("open %s (root? uinput module loaded?): %w", devPath, err)
	}
	d := &Device{f: f, name: name}

	if len(keys) > 0 {
		if err := d.ioctl(uiSetEvBit, evKey); err != nil {
			return nil, d.fail("declare EV_KEY", err)
		}
		for _, k := range keys {
			if err := d.ioctl(uiSetKeyBit, uintptr(k)); err != nil {
				return nil, d.fail(fmt.Sprintf("declare key %d", k), err)
			}
		}
	}
	if len(rels) > 0 {
		if err := d.ioctl(uiSetEvBit, evRel); err != nil {
			return nil, d.fail("declare EV_REL", err)
		}
		for _, r := range rels {
			if err := d.ioctl(uiSetRelBit, uintptr(r)); err != nil {
				return nil, d.fail(fmt.Sprintf("declare axis %d", r), err)
			}
		}
	}

	dev := userDev{ID: inputID{BusType: busVirtual, Vendor: 0x7D17, Product: 0x0001, Version: 1}}
	copy(dev.Name[:maxNameSize-1], name)
	if _, err := f.Write(asBytes(&dev)); err != nil {
		return nil, d.fail("write device descriptor", err)
	}
	if err := d.ioctl(uiDevCreate, 0); err != nil {
		return nil, d.fail("create device", err)
	}
	time.Sleep(settle)
	return d, nil
}

func (d *Device) fail(what string, err error) error {
	_ = d.f.Close()
	return fmt.Errorf("uinput %s: %s: %w", d.name, what, err)
}

func (d *Device) ioctl(req, arg uintptr) error {
	if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, d.f.Fd(), req, arg); errno != 0 {
		return errno
	}
	return nil
}

// emit writes one event. Timestamps are left zero: the kernel fills them in.
func (d *Device) emit(typ, code uint16, value int32) error {
	ev := inputEvent{Type: typ, Code: code, Value: value}
	_, err := d.f.Write(asBytes(&ev))
	return err
}

// sync ends an event packet. Nothing an input device reports is visible to
// readers until a SYN_REPORT tells them the packet is complete.
func (d *Device) sync() error { return d.emit(evSyn, synReport, 0) }

// Key presses (down) or releases a key, as one complete packet.
func (d *Device) Key(code uint16, down bool) error {
	v := int32(0)
	if down {
		v = 1
	}
	if err := d.emit(evKey, code, v); err != nil {
		return err
	}
	return d.sync()
}

// Move reports relative pointer motion.
func (d *Device) Move(dx, dy int32) error {
	if dx != 0 {
		if err := d.emit(evRel, RelX, dx); err != nil {
			return err
		}
	}
	if dy != 0 {
		if err := d.emit(evRel, RelY, dy); err != nil {
			return err
		}
	}
	if dx == 0 && dy == 0 {
		return nil
	}
	return d.sync()
}

// Close destroys the device. Anything still held down is released by the kernel
// when the device disappears, but callers should release keys themselves first
// -- a Ctrl left down in a Doom that has just exited would be a mess.
func (d *Device) Close() error {
	_ = d.ioctl(uiDevDestroy, 0)
	return d.f.Close()
}

// asBytes views a fixed-layout struct as the bytes the kernel expects. Both
// structs here are plain scalars and arrays, so their Go layout is the C one.
func asBytes[T any](v *T) []byte {
	return unsafe.Slice((*byte)(unsafe.Pointer(v)), unsafe.Sizeof(*v))
}
