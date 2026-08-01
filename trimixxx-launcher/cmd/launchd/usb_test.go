package main

import (
	"os"
	"path/filepath"
	"testing"
)

// A plain directory must never look mounted -- dj-usb creates the slot dir
// before mounting onto it, so this is what stops a premature "mounted" event.
func TestIsMountpointPlainDir(t *testing.T) {
	dir := t.TempDir()
	sub := filepath.Join(dir, "DJ_USB_1")
	if err := os.Mkdir(sub, 0o755); err != nil {
		t.Fatal(err)
	}
	if isMountpoint(sub) {
		t.Error("a freshly created directory reported as a mountpoint")
	}
	if len(mountedSet(filepath.Join(dir, "DJ_USB_*"))) != 0 {
		t.Error("mountedSet counted an unmounted directory")
	}
}
