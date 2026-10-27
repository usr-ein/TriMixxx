// Every feature's animation, by its id in data/features.ts.
import autoplay from "./autoplay";
import bluetooth from "./bluetooth";
import doom from "./doom";
import history from "./history";
import key from "./key";
import type { Scene } from "./kit";
import layout from "./layout";
import link from "./link";
import pull from "./pull";
import record from "./record";
import rekordbox from "./rekordbox";
import sync from "./sync";
import twoSticks from "./two-sticks";

export const scenes: Record<string, Scene | undefined> = {
  rekordbox,
  link,
  layout,
  sync,
  pull,
  "two-sticks": twoSticks,
  record,
  bluetooth,
  autoplay,
  doom,
  key,
  history,
};
