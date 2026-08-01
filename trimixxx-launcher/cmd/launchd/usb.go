package main

import (
	"os"
	"path/filepath"
	"syscall"
	"time"
)

// Where ../../../pi_config/dj-usb mounts DJ sticks (slots DJ_USB_1 / DJ_USB_2),
// and how often to look. Seconds of latency are irrelevant for a library
// refresh, and polling is deliberate -- see watchUSBMounts.
const (
	defaultUSBGlob = "/media/DJ_USB_*"
	defaultUSBPoll = 2 * time.Second
)

// watchUSBMounts polls for DJ_USB_* mountpoints and reports each one appearing
// and disappearing exactly once.
//
// Polling rather than inotify/udev is deliberate. pi_config/dj-usb does `mkdir`
// and only THEN `mount`, so inotify's IN_CREATE would fire on an empty
// directory before the filesystem is there, and Mixxx would rescan nothing.
// inotify cannot see the mount itself. Testing st_dev on a timer sees the state
// that actually matters, needs no dependency, and a couple of seconds of lag is
// nothing for a library refresh.
func watchUSBMounts(glob string, every time.Duration, emit func(opcode byte, path string), done <-chan struct{}) {
	// Seed from whatever is already mounted: this unit starts before Mixxx, so
	// an event fired now would go nowhere anyway.
	seen := mountedSet(glob)
	ticker := time.NewTicker(every)
	defer ticker.Stop()

	for {
		select {
		case <-done:
			return
		case <-ticker.C:
			cur := mountedSet(glob)
			for path := range cur {
				if !seen[path] {
					emit(evtUSBMounted, path)
				}
			}
			for path := range seen {
				if !cur[path] {
					emit(evtUSBUnmounted, path)
				}
			}
			seen = cur
		}
	}
}

// mountedSet returns the glob matches that are actually mounted filesystems.
func mountedSet(glob string) map[string]bool {
	set := map[string]bool{}
	matches, err := filepath.Glob(glob)
	if err != nil {
		return set // only ever a malformed pattern
	}
	for _, path := range matches {
		if isMountpoint(path) {
			set[path] = true
		}
	}
	return set
}

// isMountpoint reports whether path is the root of a mounted filesystem, by
// comparing its device to its parent's. "The directory exists" is not the same
// thing: dj-usb creates the slot dir before mounting onto it.
func isMountpoint(path string) bool {
	fi, err := os.Stat(path)
	if err != nil {
		return false
	}
	parent, err := os.Stat(filepath.Dir(path))
	if err != nil {
		return false
	}
	a, aOK := fi.Sys().(*syscall.Stat_t)
	b, bOK := parent.Sys().(*syscall.Stat_t)
	return aOK && bOK && a.Dev != b.Dev
}
