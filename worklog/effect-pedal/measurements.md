# Phase 0 measurements

Everything measured on the deck itself, at the ALSA layer, so none of it depends
on Mixxx's config being right. Recorded 2026-08-11.

Reproduce any of these with:

```sh
arecord -D hw:0,0 -c2 -f S16_LE -r44100 -d10 /tmp/x.wav
```

and the analysis snippets below. All figures are dBFS relative to 16-bit full
scale; the card is a **TI PCM2902** (`08bb:2902`), fixed-gain ADC, no capture
volume control anywhere in ALSA.

## Rig

```
CDJ ──> Xone:92 CH n ──(AUX 2, PRE-fade)──> TriMixxx codec IN
TriMixxx codec OUT ──> Xone:92 CH m        (that channel's AUX 2 send at zero)
```

Both machines on the same mains. 50 Hz country.

## 1. Converter baseline — nothing connected

The control for everything below.

| | L | R |
|---|---|---|
| Peak | −78.3 dBFS | −78.3 dBFS |
| RMS | −90.7 dBFS | −90.7 dBFS |
| Non-zero samples | 59% | 59% |

−90.7 dBFS RMS is essentially the theoretical dither floor of a 16-bit
converter, so the ADC is working and clean rather than muted. This is the number
every later noise measurement is compared against.

## 2. Signal level — CDJ playing, AUX 2 send up

| | L | R |
|---|---|---|
| Peak | −15.2 dBFS | −15.3 dBFS |
| RMS | −26.1 dBFS | −26.2 dBFS |
| DC offset | −0.6 LSB | −0.6 LSB |

- **Crest factor 10.9 dB** — normal for a mastered club track.
- **Channel balance within 0.1 dB** — both legs of the cable are good.
- **DC offset is nil**, so the codec's input coupling is fine.
- **L/R correlation 0.603.** The discriminating number: a mono source duplicated
  to both channels would read 1.000 and a dead channel near 0. The Xone's aux
  bus is stereo end to end and nothing is collapsing it.

### Calibration, settled

Three positions of the same send pot, one CDJ playing:

| Send pot | Peak | RMS | Crest |
|---|---|---|---|
| Middle | −15.2 dBFS | −26.1 dBFS | 10.9 dB |
| Fully clockwise (+6 dB) | **−0.33 dBFS** | −8.9 dBFS | 8.6 dB |
| ~1 o'clock | **−12.8 dBFS** | −21.2 dBFS | 8.5 dB |

**Settled at ~1 o'clock, −12.8 dBFS peak.** Fully clockwise was 0.33 dB from
full scale — not clipping (zero samples at FS, no flat-topped runs) but with no
headroom whatsoever, and a track mastered a decibel louder would have clipped
into a converter that has no gain control to save it.

−12.8 dBFS looks conservative for one source and is not, because **the aux bus
sums**. Two channels sending at this level peak near −7 dBFS and four could
reach full scale in the worst case, so a single channel calibrated to −9 would
leave nothing for the rest of the bus. −12.8 still gives ~77 dB SNR against the
−89.6 dBFS floor.

The crest factor briefly looked like evidence that something in the analogue
path was squashing peaks — it fell from 10.9 to 8.6 dB as the level went up, and
the RMS rose further than the peak did. It was not: the figure stayed at 8.5 dB
after backing the send off by 12.4 dB, so it tracks the passage of music in the
10-second window and nothing else. **Nothing in the path is compressing.**

Note for future calibration: the **channel VU meter is no guide here**. It shows
the pre-fader channel signal, upstream of the send pot, so it does not move when
the send does. Meter the bus with the master section's **AUX 2 switch**, which
puts the aux on the monitor meters.

## 3. Hum — everything connected and powered, all sends off, decks paused

The realistic worst case for a ground loop: both cables in place, both machines
powered from the same mains, no signal to mask anything.

| | Unconnected (control) | Connected | Δ |
|---|---|---|---|
| Broadband RMS | −90.7 dBFS | −89.6 dBFS | **+1.1 dB** |
| Peak | −78.3 dBFS | −76.3 dBFS | +2.0 dB |
| 50 Hz | −104.6 dBFS | −102.2 dBFS | +2.4 dB |
| 100 Hz | −114.5 dBFS | −105.8 dBFS | **+8.7 dB** |
| 150 Hz | −113.8 dBFS | −115.3 dBFS | −1.5 dB |
| L/R correlation | +0.246 | +0.400 | +0.154 |

Harmonic levels are Goertzel magnitudes, peak-picked over ±0.6 Hz in 0.2 Hz
steps across four one-second windows — the sweep is there because mains drifts
either side of 50 Hz and a fixed bin would under-read it.

### Reading it

**There is a mains-coupled component, and it is negligible.**

It is real: connecting the cable raised 100 Hz by 8.7 dB, and the L/R
correlation went from 0.246 to 0.400, which is what a *common-mode* source added
to both channels looks like. Independent converter noise would not correlate.

That 100 Hz is the second harmonic, and it rose far more than the 50 Hz
fundamental — the signature of full-wave rectifier ripple coupling through a
ground path, rather than magnetic pickup from a transformer, which would be
dominated by 50 Hz instead.

But the absolute levels make it a non-issue:

- Worst affected bin, 100 Hz at −105.8 dBFS, is **16 dB below the broadband
  noise floor** and would sit ~98 dB under music peaking at −8 dBFS.
- The broadband floor rose by **1.1 dB**, so the connected system is within
  about a decibel of the converter's own noise.

For scale: if the noise floor were white, a single ~1 Hz analysis bin would read
around −133 dBFS (−89.6 minus 10·log₁₀(22050)). The 100 Hz reading sits ~27 dB
above that, so it is genuinely tonal rather than noise — it is simply a tone at
a level nothing will ever hear.

**Verdict: no action. Do not chase this.** Recorded so that if hum ever does
appear — a different venue, different mains, another device sharing the earth —
there is a number to compare against rather than a guess.

### Caveats

- CDJ was *paused*, not powered off. A CDJ's own supply could contribute more
  when transports are running.
- The monitoring amplifier's state during this capture was not controlled, and
  amps are a common ground-loop partner.
- Measured at one location on one mains supply. Ground loops are a property of
  the installation, not of the equipment, so this does not transfer to a club.

## 4. Round-trip latency — 32 ms

Measured with `alsabat --roundtriplatency`, which opens playback and capture
together so the two streams share a clock — the part that makes this hard to do
with separate `aplay`/`arecord` processes. Buffer and period set to match
Mixxx's own (`-B 512 -E 256`, i.e. `latency="3"` = 256 frames) so the figure
reflects the deck's real configuration:

```sh
sudo systemctl stop getty@tty1.service
alsabat -D hw:0,0 -f S16_LE -c 2 -r 44100 -B 512 -E 256 --roundtriplatency
sudo systemctl restart getty@tty1.service
```

Path: TriMixxx codec out → Xone CH m → AUX 2 → TriMixxx codec in, with the CDJ's
send off so the bus carried only the deck. Safe to close that loop for this test
because alsabat never replays what it captures.

| Run | Result |
|---|---|
| 1 | 32 ms |
| 2 | 33 ms |
| 3 | 32 ms |

Individual probes within each run alternated between 28 ms and 34 ms. That ±3 ms
spread is one period (256 frames = 5.8 ms) of quantisation — it depends where
the impulse lands inside a buffer — so the honest figure is **32 ms ± 3**, or
about 1400 frames at 44.1 kHz.

### Where it goes, and why the 20 ms estimate was wrong

The estimate counted one buffer each way. The measurement says both directions
cost a full buffer, not a period: 512 frames out (11.6 ms) plus 512 frames in
(11.6 ms) is already 23 ms, and USB full-speed transfer plus the PCM2902's
converter group delays plus alsabat's own onset-detection bias account for the
rest. **The device is USB 1.1 full speed**, which sets a floor that no config
change reaches.

### What 32 ms changes

- **Reverb: nothing.** 32 ms reads as pre-delay, and a pre-delay that long is a
  deliberate choice in a studio, not a defect. Arguably flattering for dub-techno
  space.
- **Comb filtering is worse than estimated.** Notch spacing is `1/τ` = **31 Hz**,
  not the 50 Hz that 20 ms implied — denser and more audibly hollow. This is
  about transformative effects only (crush, filter, distortion, pitch), whose
  output stays correlated with the dry. Reverb and echo decorrelate and are
  immune.
- **Delay compensation gets harder, and has a floor.** At 128 BPM a beat is
  469 ms, so 32 ms is 6.8% of a beat — plainly audible as a late delay. Phase 4
  must subtract it. But a 1/16 note at that tempo is 29 ms, *shorter than the
  latency itself*, so short delay divisions cannot be compensated by subtraction
  at all; they would have to wrap to the following beat.

### Worth revisiting later, not now

- `latency="3"` → `2` would halve Mixxx's share (~6 ms) and, per the note in
  `mixxx_config/upload.sh`, would also cut jog-bend smear since that filter's 25
  taps are buffers rather than milliseconds. Costs xrun headroom on USB audio.
- This is alsabat's latency at Mixxx's buffer settings, not Mixxx's own. The
  definitive measurement is available once the aux input is live: close the loop
  at low gain so Mixxx regenerates, and the spacing between successive echoes in
  a capture *is* the round trip, with no synchronisation needed.

## Still outstanding
- **Sustained duplex** — 20 minutes, watching the header's underrun counter.
- **Thermals under load.** 71 °C at idle in a closed chassis, throttling around
  80–85 °C. Heat is the constraint here, not CPU, which sits at 18% of one core.
