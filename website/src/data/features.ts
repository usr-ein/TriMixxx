// What can it do? Two groups, as Sam listed them; each thing with its
// animation (lib/features/<id>.ts). The words are placeholders until Sam
// writes his own; every claim is from the build log or the release notes.

export interface Feature {
  /** Names its animation, and its row. */
  id: string;
  title: string;
  text: string;
}

export interface FeatureGroup {
  title: string;
  items: readonly Feature[];
}

export const features: readonly FeatureGroup[] = [
  {
    title: "Everything a Pioneer CDJ is able to do",
    items: [
      {
        id: "rekordbox",
        title: "Play rekordbox USB sticks",
        text: "Plug in a stick exported from rekordbox: its playlists, beat grids, hot cues, colour waveforms and covers are read straight off it. A stick of plain files works too, by folder.",
      },
      {
        id: "link",
        title: "Share them with other CDJs over the Ethernet cable",
        text: "On a Pro DJ Link network, a CDJ browses the deck's stick and loads tracks from it as it would from another CDJ, and the deck does the same with theirs.",
      },
      {
        id: "layout",
        title: "Buttons where a CDJ has them",
        text: "CUE, PLAY, the hot cues, the loops, the tempo fader and the jog wheel, a real CDJ's, sit where a CDJ has them and do what a CDJ's do. Nobody walking up to it is lost.",
      },
      {
        id: "sync",
        title: "Beat sync",
        text: "SYNC matches tempo and phase with the master, a CDJ or another deck, and the deck can be the master the CDJs follow.",
      },
    ],
  },
  {
    title: "But it can do a lot more",
    items: [
      {
        id: "pull",
        title: "Pull the stick out. No eject, no corruption",
        text: "Sticks are mounted read-only, so pulling one out mid-set can't corrupt it, and the track carries on from the deck's own copy. The one time a stick is written to is when Record set saves to it.",
      },
      {
        id: "two-sticks",
        title: "Two sticks, one deck",
        text: "Plug in two and browse both. On the link, CDJs see the second as the deck's SD card, a slot every CDJ already knows.",
      },
      {
        id: "record",
        title: "Record your set",
        text: "Record set records the mixer's output to an MP3 while you play, then writes it to your stick with the night's track list beside it.",
      },
      {
        id: "bluetooth",
        title: "A Bluetooth receiver for your phone",
        text: "Open the receiver and the deck is a speaker for your phone: its track and cover on the screen, PLAY to pause it, the loop buttons to skip.",
      },
      {
        id: "autoplay",
        title: "Autoplay, when you can't mix",
        text: "Pick a genre and a track to start from. The deck plays the genre's tracks back to back, each one the nearest in BPM to the last, none twice until all have played.",
      },
      {
        id: "doom",
        title: "Play DOOM",
        text: "Hold PLAY while the deck boots and it is a DOOM cabinet: the jog wheel turns, the pads pick the weapons.",
      },
      {
        id: "key",
        title: "Any key, not just the master's",
        text: "KEY SYNC moves the track onto the master's key, keeping its mode. Hold it to pick any of the twelve yourself.",
      },
      {
        id: "history",
        title: "History: what played, how long, with what",
        text: "Every track played on the link, by the deck or any CDJ, how long it played and what it overlapped, for the last three times the deck was on.",
      },
    ],
  },
];
