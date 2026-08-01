//go:build !linux

// uinput is a Linux kernel facility. This stub exists so the rest of the tree
// still builds, vets and tests on a development machine -- use --dry-run there,
// which prints what would be typed instead of typing it.
package uinput

import (
	"errors"
	"runtime"
)

// Mirrors of the Linux constants, so keymaps compile everywhere.
const (
	RelX = 0x00
	RelY = 0x01

	BtnLeft   = 0x110
	BtnRight  = 0x111
	BtnMiddle = 0x112
)

type Device struct{}

func New(name string, keys []uint16, rels []uint16) (*Device, error) {
	return nil, errors.New("uinput: virtual input devices are Linux-only (this is " + runtime.GOOS + "); try --dry-run")
}

func (d *Device) Key(code uint16, down bool) error { return errors.ErrUnsupported }
func (d *Device) Move(dx, dy int32) error          { return errors.ErrUnsupported }
func (d *Device) Close() error                     { return nil }
