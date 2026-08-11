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

**Verdict: about 7 dB too quiet.** Target is −8 to −10 dBFS peak. With a
fixed-gain ADC and no digital input trim, level left on the table is resolution
that cannot be recovered later. *Not yet re-measured after adjustment.*

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

## Still outstanding

- **Round-trip latency.** Still the assumed 20 ms. Measurable now: bring
  TriMixxx's own channel AUX 2 send up temporarily to close the path, stop
  Mixxx, drive `aplay` and `arecord` together, cross-correlate. Safe despite
  being the forbidden setting, because `arecord` never replays what it captures,
  so the loop never actually closes.
- **Level re-measurement** after the send pot goes up.
- **Sustained duplex** — 20 minutes, watching the header's underrun counter.
- **Thermals under load.** 71 °C at idle in a closed chassis, throttling around
  80–85 °C. Heat is the constraint here, not CPU, which sits at 18% of one core.
