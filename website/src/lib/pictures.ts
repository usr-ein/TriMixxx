// Pictures: the first site's gallery and its Hardware page's boards, filed the
// way the deck files music, into categories. The photographs and clips come
// from the media repository; the rest ships with the site.
import type { ImageMetadata } from "astro";
import { getCollection } from "astro:content";

import fig1 from "../assets/figures/ab-1-card.webp";
import fig2 from "../assets/figures/ab-2-boot.webp";
import fig4 from "../assets/figures/ab-4-update-states.webp";
import fig5 from "../assets/figures/ab-5-update-sequence.webp";
import fig7 from "../assets/figures/ab-7-workflow.webp";
import laserBack from "../assets/history/laser-play-cue-b.webp";
import laserFront from "../assets/history/laser-play-cue-f.webp";
import mainboardRouted from "../assets/history/mainboard-routed.webp";
import mainboardSchematic from "../assets/history/mainboard-schematic-2026-02-08.webp";
import oneButtonV1 from "../assets/history/onebutton-v1-layout.webp";
import sheetAudio from "../assets/history/sheet-audio.webp";
import sheetPower from "../assets/history/sheet-power.webp";
import spiceBuck from "../assets/history/spice-buck.webp";
import spiceDac from "../assets/history/spice-dac.webp";
import topCase from "../assets/history/top-case-2026-02.webp";
import v1Back from "../assets/renders/deck-v1-back.webp";
import v1Front from "../assets/renders/deck-v1-front.webp";
import v2Model from "../assets/renders/deck-v2-model.webp";
import v2Render from "../assets/renders/deck-v2-render.webp";
import schLoop from "../assets/schematics/loop.webp";
import schS3 from "../assets/schematics/midi-s3-mini.webp";
import schOneButton from "../assets/schematics/one-button.webp";
import schPlayCue from "../assets/schematics/play-cue.webp";
import autoplayGenres from "../assets/screens/autoplay-genres.webp";
import autoplayPlaying from "../assets/screens/autoplay-playing.webp";
import deckView from "../assets/screens/deck-build-and-drop.webp";
import endRecording from "../assets/screens/end-recording.webp";
import history from "../assets/screens/history-plain-plays.webp";
import homeList from "../assets/screens/after-only-the-stick.webp";
import firstSkin from "../assets/screens/playback.webp";
import firstSkinLoop from "../assets/screens/loop.webp";
import plainTags from "../assets/screens/plain-stick-tags.webp";
import trackList from "../assets/screens/browser-bpm-covers.webp";
import silencePrompt from "../assets/screens/silence-prompt.webp";
import { boards } from "../data/boards";
import { modelUrl } from "./models";

export interface Picture {
  src: ImageMetadata;
  alt: string;
  caption: string;
  date?: string | undefined;
  /** Drawn on the near-black, as diagrams and schematics are. */
  diagram?: boolean;
  /** A board: its model, to turn in 3D, and its schematics. */
  board?: {
    id: string;
    name: string;
    model: string | undefined;
    side: "top" | "bottom";
    schematics: readonly { label: string; href: string }[];
  };
  /** A clip: the video to play in place of the picture. */
  video?: string;
}

export interface Category {
  id: string;
  title: string;
  /** A line under the category's name. */
  note?: string;
  items: readonly Picture[];
}

const month = (date: Date): string =>
  date.toLocaleDateString("en-GB", { month: "short", year: "numeric" });

const videos = import.meta.glob<string>("/media/trimixxx/video/*.mp4", {
  query: "?url",
  import: "default",
  eager: true,
});

const photoTags = [
  ["decks", "deck", "The decks"],
  ["booth", "event", "In the booth"],
  ["building", "build", "Building it"],
  ["boards", "boards", "Boards"],
  ["details", "detail", "Details"],
] as const;

let cache: Category[] | undefined;

export async function categories(): Promise<Category[]> {
  if (cache) return cache;
  const photos = (await getCollection("photos")).sort(
    (a, b) => b.data.date.valueOf() - a.data.date.valueOf(),
  );
  const clips = (await getCollection("clips")).sort(
    (a, b) => b.data.date.valueOf() - a.data.date.valueOf(),
  );
  const photosOf = (tag: string): Picture[] =>
    photos
      .filter((entry) => entry.data.tags[0] === tag)
      .map((entry) => ({
        src: entry.data.image,
        alt: entry.data.alt,
        caption: entry.data.caption ?? "",
        date: month(entry.data.date),
      }));

  const boardRenders: Picture[] = boards.map((board) => ({
    src: board.image,
    alt: board.imageAlt,
    caption: `${board.name}. ${board.role}`,
    date: board.date,
    board: {
      id: board.id,
      name: board.name,
      model: modelUrl(board.id),
      side: board.side ?? "top",
      schematics: board.schematics,
    },
  }));

  const sections: Category[] = photoTags.map(([id, tag, title]) => ({
    id,
    title,
    items: id === "boards" ? [...boardRenders, ...photosOf(tag)] : photosOf(tag),
  }));

  sections.push({
    id: "clips",
    title: "Clips",
    items: clips.flatMap((entry) => {
      const video = videos[`/media/trimixxx/video/${entry.data.video.slice(2)}`];
      return video
        ? [
            {
              src: entry.data.poster,
              alt: entry.data.label,
              caption: entry.data.caption ?? entry.data.label,
              date: month(entry.data.date),
              video,
            },
          ]
        : [];
    }),
  });

  sections.push(
    {
      id: "screens",
      title: "Screens",
      note: "Most were taken on an emulated deck, whose accent is cyan; the real decks are lime and pink.",
      items: [
        {
          src: deckView,
          alt: "The deck view of Birth4000 at 126.25 BPM: a build-up with no bass, a beat of silence, then the drop in all three bands.",
          caption: "The deck view: a build, then the drop.",
          date: "Oct 2026",
        },
        {
          src: autoplayPlaying,
          alt: "The deck view during Autoplay: a House badge over the waveform and a tempo of 124.00.",
          caption: "Autoplay at work.",
          date: "Oct 2026",
        },
        {
          src: trackList,
          alt: "A rekordbox stick's tracks between 126 and 142 BPM, each with its cover, BPM and Camelot key.",
          caption: "A stick's tracks by tempo.",
          date: "Oct 2026",
        },
        {
          src: homeList,
          alt: "The home list: a stick, Autoplay, History, Record set, Diagnostics, Shut down.",
          caption: "The home list.",
          date: "Oct 2026",
        },
        {
          src: plainTags,
          alt: "A plain stick's folder listing tracks by their tags.",
          caption: "A plain stick, by folder.",
          date: "Oct 2026",
        },
        {
          src: history,
          alt: "The History list of tracks played on the link.",
          caption: "History.",
          date: "Oct 2026",
        },
        {
          src: autoplayGenres,
          alt: "Autoplay's list of genres on a stick.",
          caption: "Autoplay's genres.",
          date: "Oct 2026",
        },
        {
          src: endRecording,
          alt: "Record set running, with a level meter.",
          caption: "Record set.",
          date: "Oct 2026",
        },
        {
          src: silencePrompt,
          alt: "The prompt after five silent minutes: keep recording or end it.",
          caption: "Five silent minutes.",
          date: "Oct 2026",
        },
        {
          src: firstSkin,
          alt: "The first skin: a red waveform with beat lines, a CUE marker and a seven-segment tempo of 145.00.",
          caption: "The first skin, before the deck had a browser of its own.",
          date: "Jul 2026",
        },
        {
          src: firstSkinLoop,
          alt: "The first skin with an active loop drawn as an amber window over the waveform.",
          caption: "A loop in the first skin.",
          date: "Jul 2026",
        },
      ],
    },
    {
      id: "renders",
      title: "Renders",
      note: "The chassis in Fusion 360: the second version from September 2026, and the first from June.",
      items: [
        {
          src: v2Render,
          alt: "The V2 deck rendered: black chassis on pink legs, angled screen, jog wheel and pads.",
          caption: "V2, raytraced.",
          date: "Sep 2026",
        },
        {
          src: v2Model,
          alt: "The V2 chassis in Fusion 360 from the side, with the rear panel's sockets.",
          caption: "V2 in Fusion 360.",
          date: "Sep 2026",
        },
        {
          src: v1Front,
          alt: "The V1 chassis in Fusion 360: a grey box with an angled screen housing and a jog wheel.",
          caption: "V1, the first full chassis.",
          date: "Jun 2026",
        },
        {
          src: v1Back,
          alt: "The back of the V1 chassis in Fusion 360.",
          caption: "V1 from behind.",
          date: "Jun 2026",
        },
        {
          src: topCase,
          alt: "The first top panel in Fusion 360, a plate with the jog wheel's opening.",
          caption: "The first top panel.",
          date: "Feb 2026",
        },
      ],
    },
    {
      id: "schematics",
      title: "Schematics",
      note: "Previews; Boards links the full sheets.",
      items: [
        {
          src: schS3,
          alt: "The midi_s3_mini schematic, with handwritten notes in its margins.",
          caption: "midi_s3_mini.",
          diagram: true,
        },
        {
          src: schOneButton,
          alt: "The OneButton node's schematic.",
          caption: "one_button.",
          diagram: true,
        },
        {
          src: schPlayCue,
          alt: "The play/cue board's schematic.",
          caption: "play_cue_btn.",
          diagram: true,
        },
        {
          src: schLoop,
          alt: "The loop board's schematic.",
          caption: "loop_btn.",
          diagram: true,
        },
      ],
    },
    {
      id: "road",
      title: "The road not taken",
      note: "The monolithic main board of February and March 2026, the boards etched at home, and the first OneButton layout.",
      items: [
        {
          src: mainboardRouted,
          alt: "The monolithic board fully routed in KiCad's PCB editor.",
          caption: "Fully routed, never built.",
          date: "Mar 2026",
        },
        {
          src: mainboardSchematic,
          alt: "The whole main board on one schematic sheet.",
          caption: "The whole design on one sheet.",
          date: "Feb 2026",
        },
        {
          src: sheetAudio,
          alt: "The main board's audio output sheet.",
          caption: "Its audio outputs.",
          date: "Feb 2026",
        },
        {
          src: sheetPower,
          alt: "The main board's USB-C power delivery sheet.",
          caption: "Its power delivery.",
          date: "Feb 2026",
        },
        {
          src: spiceDac,
          alt: "A SPICE plot of the DAC's output stage.",
          caption: "SPICE: the DAC's output.",
          date: "Mar 2026",
        },
        {
          src: spiceBuck,
          alt: "A SPICE plot of the buck converter.",
          caption: "SPICE: the buck converter.",
          date: "Mar 2026",
        },
        {
          src: laserFront,
          alt: "The play/cue board's front copper as a laser file.",
          caption: "A laser file for a home-etched board.",
          date: "Jun 2026",
        },
        {
          src: laserBack,
          alt: "The play/cue board's back copper as a laser file.",
          caption: "Its other side.",
          date: "Jun 2026",
        },
        {
          src: oneButtonV1,
          alt: "The first OneButton layout in KiCad's PCB editor.",
          caption: "OneButton's first layout.",
          date: "Jul 2026",
        },
      ],
    },
    {
      id: "diagrams",
      title: "Diagrams",
      note: "The A/B update system, as drawn in its design notes.",
      items: [
        {
          src: fig1,
          alt: "The release card's eight partitions.",
          caption: "The card.",
          diagram: true,
        },
        { src: fig2, alt: "How the Pi boots slot A or B.", caption: "The boot.", diagram: true },
        {
          src: fig4,
          alt: "An update's states, from install to kept or rolled back.",
          caption: "An update's states.",
          diagram: true,
        },
        {
          src: fig5,
          alt: "The sequence of an update across RAUC, the bootloader and the health check.",
          caption: "An update, step by step.",
          diagram: true,
        },
        {
          src: fig7,
          alt: "From the repository to an emulated deck, a bench Pi and the real decks.",
          caption: "How a release travels.",
          diagram: true,
        },
      ],
    },
  );

  cache = sections.filter((section) => section.items.length > 0);
  return cache;
}

export async function pictureCount(): Promise<number> {
  return (await categories()).reduce((sum, category) => sum + category.items.length, 0);
}
