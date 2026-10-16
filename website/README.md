# website

TriMixxx's site, [trimixxx.e1n.sh](https://trimixxx.e1n.sh): what the deck is,
how it works, its hardware, a gallery, and the build log. Static,
[Astro](https://astro.build) 7, published by GitHub Pages from
`.github/workflows/website.yml` on every push to `main` that touches
`website/`.

## Working on it

Node 22.12 or newer (`.nvmrc` says 24).

```sh
npm ci
npm run media      # optional: the photographs, see below
npm run dev        # http://localhost:4321
npm run build && npm run preview
```

## Lint and format: strict, everywhere

`npm run lint` runs, with no warning allowed:

|                | Checks                                                                                                                                                                                                                                                                                   |
| -------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `format:check` | Prettier, with the Astro plugin, over everything (`npm run format` fixes)                                                                                                                                                                                                                |
| `lint:js`      | ESLint: typescript-eslint's strictest type-checked rules on `.ts`; the Astro plugin's recommended rules, its strict accessibility rules (jsx-a11y) and typescript-eslint's strict rules on `.astro`; MDX parsed, its code blocks linted, and remark-lint on the prose (`.remarkrc.json`) |
| `lint:css`     | Stylelint's standard config, on `.css` and on every `<style>` in `.astro`                                                                                                                                                                                                                |
| `lint:sh`      | shellcheck on `scripts/*.sh` (`brew install shellcheck`; GitHub's runners have it)                                                                                                                                                                                                       |
| `lint:types`   | `astro check` against Astro's `strictest` tsconfig, failing on warnings                                                                                                                                                                                                                  |

The same gates run in CI before every deploy, and as prek pre-commit hooks
(`.pre-commit-config.yaml` at the repo's root: `prek install` once): Prettier
formats the staged files, then the linters run.

## Where things are

| Path                | What                                                                                               |
| ------------------- | -------------------------------------------------------------------------------------------------- |
| `src/pages/`        | the pages: home, features, architecture, hardware, gallery, about, the build log, RSS, 404         |
| `src/content/blog/` | the build log, one MDX file per post                                                               |
| `src/components/`   | the site's pieces; `diagrams/` are the hand-drawn SVG and HTML diagrams; `mdx/` what posts can use |
| `src/assets/`       | pictures the site ships, as WebP; Astro makes AVIF/WebP in responsive widths at build              |
| `src/data/site.ts`  | the name, the pitch, the navigation, the contact address, the repositories                         |
| `public/`           | served as is: the schematics (SVG), the routing video, `og.png`, the favicons, `CNAME`             |
| `scripts/`          | the scripts that make the generated assets and fetch or add photos                                 |

## Writing a post

A post is `src/content/blog/<slug>.mdx`:

```mdx
---
title: The post's title
description: One or two sentences, shown on cards and in search results.
date: 2026-10-15 # when its phase ended; posts are ordered by it
period: Oct 2026 # what the post covers, as shown above its title
tags: [hardware, firmware]
cover: ../../assets/renders/deck-v2-render.webp
coverAlt: What the cover shows, for someone who can't see it.
coverPhoto: 2026-10-04-img-8410 # optional: a photo's id, used instead when the photos are there
---

import Figure from "../../components/Figure.astro";
import picture from "../../assets/screens/history-plain-plays.webp";

Text in Markdown.

<Figure src={picture} alt="…" caption="…" frame="screen" zoom="post" />
```

What a post can use:

- `Figure` (`src/components/Figure.astro`): a picture with a caption.
  `frame` is `none`, `screen` (a black bezel, for screenshots), `diagram` (for
  diagrams and schematics, drawn on the site's near-black, `--diagram`) or
  `dark` (for renders on transparent backgrounds). `zoom` opens it in the lightbox; pictures with the same `zoom`
  value form a set the arrow keys step through.
- `Gallery` (`mdx/Gallery.astro`): two or three figures side by side.
- `Callout` (`mdx/Callout.astro`): an aside; `kind` is `note`, `verify` (for
  what has only been shown on emulated decks) or `quote`.
- `Video` (`mdx/Video.astro`): a silent loop with a poster frame.
- `Photo` (`mdx/Photo.astro`): a photograph from the media repository, by id.
- `Clip` (`mdx/Clip.astro`): a short silent clip from the media repository, by id.
- `BoardModel` (`src/components/BoardModel.astro`): a board's render that opens
  the board in 3D, by its id on the hardware page (`board="one-button"`); with
  a `caption`, framed for a post.
- The diagrams in `src/components/diagrams/`.

**What doesn't go on the site:** secrets, hostnames, IP or MAC addresses,
serial numbers, ssh aliases or Wi-Fi names or passwords. A screenshot of a
CDJ's interface (its firmware, Pioneer's, draws it) only comes from the media
repository, never this one, with a disclaimer in its caption. Say when
something has only been shown on emulated decks.

## Photographs

The photos don't live in this repository. They are in
[usr-ein/static-resources](https://github.com/usr-ein/static-resources), in
`trimixxx/photos/`: a JPEG and a YAML file per photo. `npm run media` clones
that folder, shallow and sparse, into `media/` (ignored here); the build reads
it as a content collection, and Astro makes the responsive AVIF/WebP versions.
Without the clone the site builds with no photos; in CI a failed clone fails
the build.

To add photos:

```sh
npm run media
sh scripts/add-photos.sh --tag event ~/Downloads/IMG_8410.HEIC ...
# --crop L,T,R,B keeps a rectangle, in percent of the upright photo; --rotate 90|-90|180
sh scripts/add-photos.sh --tag event --crop 0,32,100,74 ~/Downloads/IMG_9281.HEIC
# fill in each new media/trimixxx/photos/*.yaml: alt (required), caption, tags
npm run build                    # checks every YAML against the schema
git -C media add trimixxx && git -C media commit -m "trimixxx: …" && git -C media push
```

`add-photos.sh` turns each original upright, resizes it to at most 2560 px,
re-encodes it as a JPEG, and strips **all** of its metadata, the GPS position
included, refusing any photo where GPS data survives. The YAML gets the capture
date from the original. A photo's `tags` place it in the gallery (its first tag
picks the section: `deck`, `event`, `build`, `boards` or `detail`; `screen` keeps
a screenshot out of it); a post shows one with `<Photo id="…" />`, or uses one
as its cover with `coverPhoto`.

### Clips

Short clips sit beside the photos, in `trimixxx/video/`: an MP4, its poster
frame and a YAML file. `scripts/add-video.sh` makes them:

```sh
FFMPEG=/path/to/working/ffmpeg sh scripts/add-video.sh --from 00:00:03 --to 00:00:12 ~/Downloads/IMG_7047.MOV
# fill in media/trimixxx/video/<id>.yaml: label (required), caption, tags
```

It cuts the clip, scales it to at most 1280 px wide, drops the sound unless
`--keep-audio`, drops all metadata (a phone's video records where it was
filmed) and refuses the clip if a location survives. A post plays one, muted
and looping, with `<Clip id="…" />`.

### Board models

Each board on the hardware page opens in 3D from its render: a GLB in
`trimixxx/models/<board id>.glb`, beside the photos. `sh scripts/export-boards.sh
models` makes them with KiCad's GLB export; `scripts/kicad-glb-colors.mjs` then
decodes the colours KiCad writes as sRGB where glTF wants linear, and gltfpack
compresses them (24 MB to 1.4 MB for the controller). Commit them to the media
repository like photos. The viewer (`ModelViewer.astro`, `src/lib/board-viewer.ts`)
is three.js, downloaded only when someone opens a board.

## Generated assets

Made on a Mac, then committed:

- `sh scripts/export-boards.sh [renders] [schematics] [models]`: each board's
  3D render, its schematics (in KiCad's Vampire theme, on the site's
  near-black) and its model, from the KiCad sources in `../boards`; the
  never-built main board's model from the last commit that had it (needs
  KiCad 10, `rsvg-convert`, `cwebp`).
- `node scripts/dark-figures.mjs`: the A/B diagrams in `src/assets/figures/`,
  the SVGs of `pi-qemu/rauc-pi-4-report.html` in its dark palette (needs
  `npm install --no-save puppeteer-core@25.12.0` and Chrome).
- `node scripts/make-covers.mjs`: the build log's drawn covers and
  `public/og.png`, the picture shown when a link to the site is shared.

## Link previews

A shared link shows the page's title, its description and a picture, cut to
1200 × 630 by `Base.astro`: a post's cover (its `coverPhoto` when the photos
are there), the photo a page passes as `image`, or else `public/og.png`. A new
page passes one with `image={share?.data.image} imageAlt={share?.data.alt}`,
where `share` is a photo by id.

## Publishing

Pushing to `main` runs the workflow: install, fetch the photos, lint, build,
deploy. GitHub Pages' source is "GitHub Actions" and its custom domain is
`trimixxx.e1n.sh`, whose DNS points at GitHub Pages. A pull request touching
`website/` gets the same lint and build, without the deploy.

Visits are counted by Google Analytics (`analytics` in `src/data/site.ts`),
only once a visitor accepts it in the consent banner (`Consent.astro`,
`src/lib/analytics.ts`), and only on `trimixxx.e1n.sh`: local builds and test
runs are never counted. The choice is kept in the browser; the footer's
"Analytics settings" asks again, and declining then deletes GA's cookies.
