# TriMixxx website v2: design doc

27 October 2026. Built on the branch `website-v2` in `website_v2/`, then
swapped in for the first site's `website/` the same day.

## The idea

**The website is the deck.**

You arrive to the deck itself, drawn in lines, turning slowly in the dark. Spin
it, pinch it. Tap it and it switches on: the camera falls into its screen, the
boot splash flickers up, and you are looking at a track on the deck's own
waveform, with the BACK button and the knob beside the glass, as on the real
one.

From then on you use the website the way you use the deck. Turn the knob, push
it to go in, BACK to come out. The build log, the pictures, what it can do and
why it exists are rows in the deck's root list, beside the stick with the music
on it.

Nothing on it sells anything. It is a toy, made for fun and to share how the
thing was made.

## What goes, and why

The current site converged on the shape every product site has: a tagline, two
call-to-action buttons, chips, a three-card manifesto, a feature grid, and
section after section down one long scroll. Every page says everything at once,
nothing says what matters first, and it reads as AI-made.

None of it carries over. Not in v2:

- taglines, kickers above headings, ledes, slogans
- call-to-action buttons, "read more →" links, pills, chips
- card grids (icon, title, two lines), feature grids, numbers-as-proof
- full-width sections stacked down a long scroll, each with its heading
- gradients, glows behind text, stock icons, emoji
- a footer full of links

## Principles

1. **One thing at a time.** Hierarchy comes from depth, not from scrolling.
   Each level shows one thing; you go deeper to see more.
2. **Only the deck's own language.** Its colours, its font, its rows, its
   breadcrumb, its knob and BACK button, its boot splash. If the deck doesn't
   have it, neither does the site, unless it is a joke the deck would make.
3. **Touch before text.** Every screen has something to play with.
4. **Weird beats generic.** When two designs work, pick the stranger one.
5. **Honest.** The rules of the current site still hold: say what was only
   shown on emulated decks; no hostnames, IPs, serials or Wi-Fi details;
   Pioneer's UI only from the media repository, with its disclaimer.

## The levels

```
0  the deck in 3D, drawn in lines           landing
1  its screen: the deck view, a track       after the tap
2  the root list                            the stick, the four pages, Shut down
3  a page                                   Build log · Pictures · What can it do? · Why build it
4  one item                                 a post, a picture, a feature
```

## Walkthrough

### 0. The wireframe

- A black page and the V2 deck in lines, from Fusion's export of the whole
  assembly (`scripts/deck-model.mjs`): each body's sharp edges, the ones
  hidden behind something drawn faint, so the boards and pads show through the
  top plate. The CDJ jog, which the model leaves out, is drawn into its hole.
  It turns slowly by itself.
- Drag spins it; pinch, or the scroll wheel, zooms; two fingers move it. Left
  alone, it goes back to turning.
- The lines take the colours of the parts: the black body in grey, the legs,
  the screen's band and the fader slot pink, the jog's centre glowing green as
  on the real deck.
- The screen is the one lit surface: the deck's own boot splash
  (`pi_config/trimixxx_logo_crt.svg`: TriMixxx in green pixels, "Deck ready"),
  its cursor blinking. That is where to tap.
- No words on the page. The only text is what is on the deck: its labels, and
  the name on its splash.
- A tap on the deck (not the end of a drag) switches it on; Enter does the same
  from the keyboard.

### 1. Power on

- The camera swings round to face the screen and falls into it until the
  splash fills the window, then the tube flickers once.
- The deck view takes over from it, filling the window, and draws back until
  the frame is around it. About three seconds in all.

### 2. The deck view: the demo

The deck's playback screen, working (as in
`website/src/assets/screens/deck-build-and-drop.webp`):

- the header: cover, title, key, artist, phase meter, SYNC and MASTER, elapsed
  and remaining time;
- the scrolling waveform in three bands, lows blue at the back, mids yellow,
  highs white in front; a line on every beat, red on the downbeats; the CUE
  mark; the playhead in the middle;
- the tempo box: pitch, range, and the BPM in orange seven segments;
- the overview of the whole track along the bottom.

| Gesture                  | What it does                                                                                           |
| ------------------------ | ------------------------------------------------------------------------------------------------------ |
| Tap the waveform         | Play / pause                                                                                           |
| Drag it sideways         | A hand on the platter: the sound follows the finger, backwards too; let go and it carries on as before |
| Tap or drag the overview | Jump there                                                                                             |
| Turn the knob            | Zoom the waveform, as the deck's encoder does in this view                                             |
| Push the knob, or BACK   | Open the root list                                                                                     |

The music never starts by itself; a tap starts it. It keeps playing while you
browse, as on the deck.

### 3. The frame

The top of the V2's screen housing, as in the photos
(`2026-10-10-img-9308`): matte black printed plastic with its layer lines, the
glass and its bezel, the printed label plates. Right of the screen, BACK above
the knob: a frosted, translucent silver button that presses in, and a knurled
black knob with a white line that turns in detents. Drawn in CSS and SVG, not
photographs, so it scales and the knob can really turn.

Left of the screen, as on the real deck: SORT/INFO, two LEDs and the double USB
port, there for looks only (Sam, 27 Oct). They do nothing.

On a phone held upright, the screen fills the width and the controls move under
it, in thumb reach.

### 4. The top bar and the root list

The top bar is the deck's breadcrumb, on every screen including the deck view:
the house, then where you are (`⌂ › Build log › A night at Technobase`). The
house opens the root list.

The root list is the deck's level 0 (`before-root-ro-listed.webp`), with the
pages as its rows (Sam, 27 Oct):

```
⌂
›  ▯  1  TRIMIXXX            1 track        the stick with the demo track
›  ◷  Build log              13 posts
›  ▣  Pictures               86 photos
›  ?  What can it do?        12 things
›  i  Why build it
   ⏻  Shut down                              back to the wireframe
```

The controls are the deck's (`docs/browser-prd.md` §4):

| Input       | Mouse / touch                              | Keyboard       | Does                                             |
| ----------- | ------------------------------------------ | -------------- | ------------------------------------------------ |
| Knob turn   | Drag around the knob, scroll wheel over it | ↑ ↓            | Move the selection one row                       |
| Knob push   | Click or tap the knob                      | Enter          | Go in; on a track, load it                       |
| BACK        | Click or tap                               | Backspace, Esc | Up one level; at the root, back to the deck view |
| Hold BACK   | Hold                                       | Hold Esc       | Straight back to the deck view                   |
| Tap a row   | Tap                                        |                | Go in                                            |
| Swipe right | Swipe                                      |                | BACK                                             |

Every screen has its own URL, so the browser's back button and the deck's BACK
do the same thing, and a link to a post opens the deck already on, with the
post on its screen.

**Shut down** does what it says: the picture collapses to a line and a dot,
the camera pulls back, and you are at the wireframe again.

### 5. Build log

The deck's History screen (`history-plain-plays.webp`), one row per post: the
cover, the title, the period and tags where the artist goes, and where History
shows how long a track played, how long the post takes to read (`7m`). Newest
first.

Opening a post puts it on the screen as a page you scroll, with the knob or a
finger, like the deck's Diagnostics page. The posts stay as they are for now
(Sam will rewrite them); their figures, photos, clips and boards in 3D are
restyled for the screen.

### 6. Pictures

The gallery's photos, filed the way the deck files music: the root row opens
categories (The decks, In the booth, Building it, Boards, Details), each a list
in the deck's info layout (`browser-prd.md` §8.2). Captions on the left, the
selected picture large on the right where the artwork goes, its date and tags
under it. Push the knob for the picture alone, as large as the screen allows.

The Hardware page's boards move here (Sam, 27 Oct): in Boards, each board's
render still opens it in 3D, with its schematic beside it. The Architecture
page's diagrams stay only in the posts that use them.

### 7. What can it do?

One list, two groups, in the info layout: the feature on the left, and on the
right, where the artwork goes, its animation, with two or three sentences under
it. On a phone each row carries its animation beside its text.

Each animation is a small loop drawn in the deck's own style (its colours, its
rows, its seven segments): no stock illustrations. Paused when off screen; a
still frame with reduced motion.

**Everything a Pioneer CDJ is able to do**

| Feature                                                     | Animation                                                                                                                                        |
| ----------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| Play rekordbox USB sticks                                   | A stick slides into the port; its row appears in the root list; a playlist fills in row by row; the waveform draws itself in, band by band       |
| Share their content with other CDJs over the Ethernet cable | The deck and a CDJ in outline, joined by a cable; packets run along it; the CDJ's screen lists the deck's stick and loads a track from it        |
| Buttons in the same place, doing the same thing             | A CDJ's top panel turns into the TriMixxx's; CUE, PLAY, the hot cues and the loop buttons stay where they are while the body changes around them |
| Beat sync                                                   | Two waveforms drifting out of step, the phase meter wobbling; SYNC lights; the lower one slides into step and its BPM rolls over to the master's |

**But it can do a lot more**

| Feature                                                | Animation                                                                                                                                                                                    |
| ------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Pull the stick out with no eject, and never corrupt it | The stick is yanked out mid-track; the waveform keeps scrolling; the deck's own toast: "SAM1 removed — current track is cached and stays playable."                                          |
| Two sticks, one deck                                   | Two sticks go in; the root list grows a second source; a CDJ on the link sees it in its SD slot                                                                                              |
| Record your set on the deck                            | REC, a seven-segment clock running, the night's waveform piling up; End recording; the files appear on the stick under the deck's own names (`recorded-set-00004207.mp3` and its track list) |
| A Bluetooth receiver for your phone                    | A phone in outline; the deck's receiver page shows what it plays, cover and all; PLAY on the deck pauses the phone, the loop buttons skip                                                    |
| Autoplay when you can't mix                            | Under a genre badge, a track ends and the next starts at once, the nearest in BPM, the seven segments ticking over; the jog turning with nobody at it. No crossfade: Autoplay has none       |
| Play DOOM                                              | The jog turns and a tiny corridor turns with it (drawn here, none of id's art) while the pads light up as weapons                                                                            |
| Change the track's key to any key                      | A Camelot wheel; the key jumps 8A → 3A → 11A; the key badge turns KEY SYNC purple                                                                                                            |
| History: what played, how long, mixed with what        | Track bars laid on a timeline, overlapping where two played together, each with how long it played                                                                                           |

Words to get right: sticks are mounted read-only except while Record set writes
the recording to one, so "never corrupt" needs that said. Which of these were
shown on a real deck and which only on emulated ones gets checked against the
build log before it goes live.

### 8. Why build it

The "Why build a deck at all" text from the About page, signed, as one page
that scrolls. After it, from the same page: the projects it is built on, the
repositories and their licences, trademarks and affiliation, and how to get in
touch (`trimixxx@e1n.sh`, GitHub issues).

### 9. Odds and ends

- **404:** a deck screen with a prompt, in the style of the deck's own toasts.
- **Analytics:** Google Analytics stays as it is (only after consent, only on
  trimixxx.e1n.sh); the consent question becomes a deck toast.
- **RSS** stays at `/rss.xml`.
- **Old URLs** redirect: `/features/` → What can it do?, `/gallery/` →
  Pictures, `/about/` → Why build it; posts keep `/blog/<slug>/`.

## Look and feel

The deck's skin (`mixxx_config/TriMixxx_skin`) is the style guide.

| What           | Value                                                                                                  |
| -------------- | ------------------------------------------------------------------------------------------------------ |
| Ground         | `#000000` screen, `#0c0c0c` panels, `#1a1a1a` / `#2a2a2a` rows and lines                               |
| Text           | `#e0e0e0`, dimmed `#909090`                                                                            |
| Accent         | trimixxx2's pink, `#FF7092` (trimixxx1 is lime `#88ff00`): the selected row, where you are             |
| Waveform       | lows `#2266ff`, mids `#ffcc00`, highs `#ffffff`; beats `#cccccc`, downbeats `#ff0000`, CUE `#ff0033`   |
| Seven segments | orange `#ff8800`, drawn as segments (Digital-7 is freeware for home use only, so not on a public site) |
| KEY SYNC       | purple `#b06cff`                                                                                       |
| Type           | the deck's font, Meslo LG (Apache-2.0), for everything, posts included                                 |
| Frame          | matte black printed plastic, frosted silver buttons, a black knurled knob, pink where the deck is pink |

Motion is mechanical: things switch, slide and click; nothing floats. The only
flourishes are the CRT on power-on and Shut down.

## How it is built

- **Astro 7, static**, as now: MDX posts, content collections for the posts
  and photos, GitHub Pages, the same strict lint gates (Prettier, ESLint strict
  and a11y, Stylelint, `astro check`).
- **One frame, many screens.** Astro's client router keeps the frame and the
  audio alive across pages (`transition:persist`), so only the screen changes
  and the music never stops. Every page is still a real HTML page that reads
  without JavaScript.
- **The wireframe.** `npm run model -- Trimixxx-V2.obj` turns Fusion's OBJ
  export into lines, once, on a Mac: each body's edges where its faces meet at
  more than 26°, copper and parts under 10 mm left out, the outer bodies kept
  as the solid that hides lines and catches taps, the screen's rectangle and
  the jog's hole found from the geometry. The result (600 KB, gzipped) goes to the media
  repository (static-resources `trimixxx/models/deck-v2.gzbin`), like the boards'
  GLBs. three.js draws it, loaded only on the landing; without it, a stand-in
  drawn from the dimensions.
- **The deck view.** The waveform on a canvas. The sound through Web Audio, an
  AudioWorklet that reads the track at any speed, backwards included, which is
  what makes scratching work. The three bands, beats and grid are worked out at
  build time from the track and shipped as a small file beside it.
- **Without WebGL, or with reduced motion,** the landing shows a still
  drawing of the deck, with the same tap.

## What carries over

| Now                                 | In v2                                              |
| ----------------------------------- | -------------------------------------------------- |
| Home                                | Gone. The wireframe                                |
| Features                            | What can it do?                                    |
| Architecture                        | Gone; its diagrams stay in the posts that use them |
| Hardware (boards in 3D, schematics) | Pictures › Boards                                  |
| Gallery                             | Pictures                                           |
| Build log, 13 posts                 | Build log, the posts unchanged for now             |
| About                               | Why build it                                       |
| 404, RSS, analytics                 | Kept, restyled                                     |

## Order of work

1. The project, beside the first site: routes, the lint setup, the posts and
   photos wired in.
2. The landing, with a stand-in model drawn from `ref_sizes/dims_v2_model.md`
   until the STEP arrives, then the real one.
3. Power on, the frame, desktop and phone.
4. The deck view: waveform, sound, gestures.
5. The root list and the navigation: knob, BACK, keys, touch, URLs.
6. Build log, Pictures, Why build it.
7. What can it do?, and its twelve animations.
8. 404, redirects, RSS, analytics, accessibility and speed, lint.
9. Running locally for a demo; then the swap.

## Where it stands (27 Oct)

All of it is built and swapped in: the first site is gone, and this one is
published from `main`. The deck's model and its track, "On / Offline", are in
the media repository. The workflow installs ffmpeg, which `npm run build`
needs to make the stand-in loop when there is no track. Left:

- Sam's words: the features' texts (`src/data/features.ts`) are placeholders,
  and the posts are the first site's, to be rewritten.
- Seen only in headless Chrome so far: Safari and Firefox, and a real phone
  (touch, the mute switch, the sound's memory on a long track).

## Open questions

- The wireframe: for now the visible edges bright and the hidden ones faint,
  so the insides show through a little. All bright (an X-ray), or only the
  outside (a technical drawing), are one number each in `lib/landing/model.ts`.
- Clicks on the knob and buttons, or silent? Silent until decided.

## Decided

- 27 Oct: the 3D model comes from a STEP of the full deck, exported by Sam.
- 27 Oct: the Build log looks like the deck's History.
- 27 Oct: the pages are rows of the deck's root list.
- 27 Oct: the demo plays a track Sam holds the rights to. It lives in the media
  repository, never in this one. Until it arrives, a loop synthesised at build
  time stands in.
- 27 Oct: nothing is written on the landing.
- 27 Oct: Hardware's boards go to Pictures › Boards, still in 3D; Architecture
  goes.
- 27 Oct: SORT/INFO, the LEDs and the USB port are on the frame, for looks.
- 27 Oct: the model is Sam's export of the whole V2 (OBJ, beside the STEP), its
  lines made by `scripts/deck-model.mjs` into the media repository.
- 27 Oct: "On / Offline" plays in the local demo, for now (Sam). Its grid was
  found by `scripts/track.mjs`: 150.00 BPM, first downbeat at 3.458 s.
- 27 Oct: "On / Offline" goes on the site, with UFO95's written agreement
  (Sam), credited on Why build it.
- 27 Oct, while building: the idle deck shows its boot splash rather than a
  waveform, so the dive lands on the splash and the name appears only on the
  deck itself.
