// The demo track, at build time: Sam's, from the media repository
// (media/trimixxx/track/, made by scripts/track.mjs), or else the stand-in the
// same script synthesises into public/track/.
import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";

import type { Track } from "./waveform";

export interface DemoTrack extends Track {
  audio: string;
  wave: string;
  cover: string | undefined;
  standin: boolean;
}

const files = import.meta.glob<string>("/media/trimixxx/track/*.{m4a,wave,jpg}", {
  query: "?url",
  import: "default",
  eager: true,
});
const metas = import.meta.glob<Track>("/media/trimixxx/track/track.json", {
  import: "default",
  eager: true,
});

export function demoTrack(): DemoTrack {
  const real = metas["/media/trimixxx/track/track.json"];
  const audio = files["/media/trimixxx/track/track.m4a"];
  const wave = files["/media/trimixxx/track/track.wave"];
  if (real && audio && wave) {
    return {
      ...real,
      audio,
      wave,
      cover: files["/media/trimixxx/track/cover.jpg"],
      standin: false,
    };
  }
  const standin = join(process.cwd(), "public/track/track.json");
  if (!existsSync(standin)) {
    throw new Error("No demo track: run `npm run track:standin`");
  }
  const meta = JSON.parse(readFileSync(standin, "utf8")) as Track;
  return {
    ...meta,
    audio: "/track/track.m4a",
    wave: "/track/track.wave",
    cover: undefined,
    standin: true,
  };
}
