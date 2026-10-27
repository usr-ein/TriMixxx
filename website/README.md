# website

TriMixxx's site, [trimixxx.e1n.sh](https://trimixxx.e1n.sh). The site is the
deck: the landing is the V2 in 3D, drawn in lines; a tap switches it on, and
every page after that is a screen of the deck, in its frame, worked with its
knob and its BACK button. Why it looks like this, and what it must not turn
into again: `design_doc.md`. Static, [Astro](https://astro.build) 7, published
by GitHub Pages from `.github/workflows/website.yml`.

## Working on it

Node 22.12 or newer (`.nvmrc` says 24), and ffmpeg.

```sh
npm ci
npm run media      # the photographs, the boards' and the deck's models, the demo track
npm run dev        # http://localhost:4321
npm run build && npm run preview
```

## Where things are

| Path                     | What                                                                                                |
| ------------------------ | --------------------------------------------------------------------------------------------------- |
| `src/pages/`             | the landing, the deck view (`deck`), the root list (`browse/`), the four pages, the posts, RSS, 404 |
| `src/layouts/`           | `Shell` (every page's document, the client router, the CRT), `Deck` (a screen in its frame)         |
| `src/components/deck/`   | the frame (`Housing`) and the deck view                                                             |
| `src/components/screen/` | what the deck's screen is made of: the breadcrumb, rows, the info layout, icons, seven segments     |
| `src/lib/deck/`          | the controls (knob, BACK, keys, touch), the sound (`engine`, `processor`), the waveforms, the CRT   |
| `src/lib/landing/`       | the landing's 3D scene, the deck's model and its stand-in                                           |
| `src/lib/features/`      | What can it do?'s animations, one file each, drawn with `kit.ts`                                    |
| `src/data/`              | the site, the features' words, the boards                                                           |
| `src/content/blog/`      | the build log, one MDX file per post                                                                |
| `src/styles/deck.css`    | the deck's look: the skin's colours, the printed frame in millimetres, the screen's rows            |
| `public/`                | the fonts (the deck's Meslo), the boot splash, the schematics, `og.png`, the favicons               |
| `scripts/`               | the deck's model, the demo track, and the photos' and boards' tools                                 |

## How the deck works here

The controls are the deck's (`docs/browser-prd.md` §4). The knob, turned with
a drag around it, the wheel or ↑ ↓, moves the selection; pushed (a click,
Enter), it goes in. BACK (Esc) goes up a level, and from the root list back to
the track; held, straight back to the track. On a phone, a swipe to the right
is BACK. In the deck view, a tap on the waveform plays or stops, a drag
scratches, a tap on the strip under it jumps, the knob zooms, Space plays.

The client router keeps the sound alive between pages, so the track plays on
while you browse; every page is still a real page, which reads without
JavaScript.

## The deck's model

The landing draws `media/trimixxx/models/deck-v2.gzbin`, made from Fusion's OBJ
export of the whole V2 assembly:

```sh
npm run model -- ~/Downloads/Trimixxx-V2.obj
git -C media add trimixxx/models/deck-v2.gzbin && git -C media commit -m "trimixxx: the V2's lines" && git -C media push
```

It keeps each body's edges where its faces meet at more than 26°, leaves out
the copper and anything under 10 mm, keeps the outer bodies as the solid that
hides the lines behind it and catches taps, and finds the screen's rectangle
and the jog's hole (the model has no jog; the landing draws the CDJ's into
it). Without the file, the landing draws a stand-in from the deck's
dimensions.

## The demo track

The deck view plays `media/trimixxx/track/`: the audio, its three bands and
grid (`track.wave`, `track.json`) and its cover, made by `scripts/track.mjs`:

```sh
npm run track -- song.mp3 --bpm 150 --title "…" --artist "…" --key 3A
```

It finds the grid itself (the BPM to a hundredth around `--bpm`, the first
downbeat on the first kick) unless given `--first-beat`. A track goes to the
media repository, and so onto the public site, **only if the site may publish
it**. Without one, `npm run dev` and `npm run build` synthesise a stand-in loop
into `public/track/` (ignored by git).

## Lint and format: strict, everywhere

`npm run lint` runs, with no warning allowed: Prettier, ESLint
(typescript-eslint's strictest type-checked rules on `.ts`, the Astro plugin's
strict accessibility rules on `.astro`, MDX parsed and remark-linted),
Stylelint, shellcheck on `scripts/*.sh`, the inline-space check and
`astro check`. The same gates run in CI before every deploy.

## Writing a post

A post is `src/content/blog/<slug>.mdx`, with `title`, `description`, `date`,
`period`, `tags`, `cover`, `coverAlt` and optionally `coverPhoto` in its front
matter. It can use `Figure`, `mdx/Gallery`, `mdx/Callout` (`note`, `verify`,
`quote`), `mdx/Video`, `mdx/Photo` and `mdx/Clip` (by id, from the media
repository), `BoardModel` (a board in 3D) and the diagrams in
`components/diagrams/`. On the deck it is a row of the build log, like a track
in its History, and opens as a page the knob scrolls.

**What doesn't go on the site:** secrets, hostnames, IP or MAC addresses,
serial numbers, ssh aliases or Wi-Fi names or passwords. A screenshot of a
CDJ's interface (Pioneer's firmware draws it) only comes from the media
repository, with a disclaimer in its caption. Say when something has only been
shown on emulated decks.

## Photographs, clips and boards

They live in [usr-ein/static-resources](https://github.com/usr-ein/static-resources),
`trimixxx/`, cloned into `media/` by `npm run media`. `scripts/add-photos.sh`
and `scripts/add-video.sh` add them, stripped of their metadata;
`scripts/export-boards.sh` makes the boards' renders, schematics and models.
A photo's first tag files it in Pictures (`deck`, `event`, `build`, `boards`,
`detail`; `screen` keeps it out).

## Analytics

Google Analytics (`analytics` in `src/data/site.ts`) counts a visit only once
the visitor says yes to the deck's toast, and only on `trimixxx.e1n.sh`. The
choice is kept in the browser; "Analytics settings", on Why build it, asks
again.
