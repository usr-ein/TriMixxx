// The info layout's panel follows the selection: the selected row's item is
// shown, the others hidden (their pictures load only once shown), a clip
// plays only while it is the one shown. Activating the row (the knob pushed,
// or a second tap) opens the item: its picture in the lightbox, its board in
// 3D, its clip played or stopped.

document.addEventListener("deck:select", (event) => {
  const row = (event as CustomEvent<HTMLElement>).detail;
  const index = row.dataset.index;
  if (index === undefined) return;
  for (const art of document.querySelectorAll<HTMLElement>("[data-art]")) {
    const shown = art.dataset.art === index;
    art.hidden = !shown;
    const video = art.querySelector("video");
    if (video && !shown) video.pause();
  }
});

document.addEventListener("deck:activate", (event) => {
  const row = (event as CustomEvent<HTMLElement>).detail;
  if (row.dataset.action !== "open") return;
  const art = document.querySelector(`[data-art="${row.dataset.index ?? ""}"]`);
  const opener = art?.querySelector<HTMLElement>("[data-lightbox], [data-model], video");
  if (opener instanceof HTMLVideoElement) {
    if (opener.paused) void opener.play().catch(() => undefined);
    else opener.pause();
  } else {
    opener?.click();
  }
});
