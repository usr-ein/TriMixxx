# Wi-Fi fallback

At a venue there is no home Wi-Fi, and the deck still has to be reachable for
a last-minute fix. So at boot, if wlan0 has not joined a network within 45 s
(30 s more if a connection is still on its way), the deck becomes one: an
access point named after its hostname.

| | |
|---|---|
| Network | the deck's hostname, e.g. `trimixxx2` |
| Password | `trimixxx-debug-Kmj3Df` |
| Radio | 2.4 GHz, one channel per deck, hardcoded in `install.sh`: Trimixxx1 6, trimixxx2 7, the next deck 8 |
| The deck | `10.42.0.1`: `ssh sam1902@10.42.0.1`, or `ssh sam1902@trimixxx2.local` |

Decided once per boot, and never undone: the deck does not go back to home
Wi-Fi by itself. A reboot tries home again.

The Diagnostics page says which it is — the home network, or the hotspot with
its password — and shows the Ethernet address too. That is the other way in: a
laptop on the CDJs' switch can ssh to it, since sshd listens on every interface.

## Files
- `trimixxx-wifi-fallback` — the decision, and the safety rules it keeps (its
  header). Writes what it decided to `/run/trimixxx/wifi`, for Diagnostics.
- `trimixxx-wifi-fallback.service` — after NetworkManager; nothing waits for it,
  so a boot without home Wi-Fi is as fast as one with it.
- `install.sh` — the script, the unit (enabled for the next boot, not started)
  and the `trimixxx-hotspot` profile (`autoconnect=no`). Inert: it checks that
  wlan0 and the default route are unchanged. Run by `../upload.sh`.

## Trying it at home
```sh
ssh trimixxx-pi-2 'echo 300 | sudo tee /var/lib/trimixxx/wifi-fallback-test && sudo reboot'
```
That boot, and only that one, stands in for a venue: it drops home Wi-Fi, the
hotspot appears about 45 s in, holds for 300 s, and then the deck rejoins home
by itself. A reboot at any point is a normal boot.

## Getting back in
- Power cycle: home Wi-Fi is tried at every boot.
- The hotspot itself.
- ssh over Ethernet, from the CDJs' switch.
- `sudo systemctl disable trimixxx-wifi-fallback` takes the behaviour out
  entirely.
