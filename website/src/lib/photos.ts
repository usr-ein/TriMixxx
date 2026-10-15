import type { ImageMetadata } from "astro";
import { type CollectionEntry, getCollection, getEntry } from "astro:content";

/**
 * A photo from the media repository, by id (its YAML file's name).
 *
 * Without the media clone the collection is empty, and every lookup quietly
 * returns undefined, so the site still builds. With it, an id that matches no
 * photo is a typo, and fails the build rather than dropping a picture.
 */
export async function photo(id: string): Promise<CollectionEntry<"photos"> | undefined> {
  const entry = await getEntry("photos", id);
  if (!entry && (await getCollection("photos")).length > 0) {
    throw new Error(`No photo "${id}" in media/trimixxx/photos`);
  }
  return entry;
}

/** A post's cover: its photo when it names one and the photo is there, else its own image. */
export async function coverOf(
  post: CollectionEntry<"blog">,
): Promise<{ src: ImageMetadata; alt: string }> {
  const picked = post.data.coverPhoto ? await photo(post.data.coverPhoto) : undefined;
  return picked
    ? { src: picked.data.image, alt: picked.data.alt }
    : { src: post.data.cover, alt: post.data.coverAlt };
}

/** A clip from the media repository, by id; the same rules as photo(). */
export async function clip(id: string): Promise<CollectionEntry<"clips"> | undefined> {
  const entry = await getEntry("clips", id);
  if (!entry && (await getCollection("clips")).length > 0) {
    throw new Error(`No clip "${id}" in media/trimixxx/video`);
  }
  return entry;
}
