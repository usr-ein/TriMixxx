// The boards' 3D models (GLB), from the media repository: written by
// scripts/export-boards.sh, fetched by `npm run media`. Without the clone,
// there are none, and the boards' renders stay pictures.
const files = import.meta.glob<string>("/media/trimixxx/models/*.glb", {
  query: "?url",
  import: "default",
  eager: true,
});

/** The URL of a board's model, by its id on the hardware page, if there is one. */
export function modelUrl(board: string): string | undefined {
  return files[`/media/trimixxx/models/${board}.glb`];
}
