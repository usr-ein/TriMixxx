// The build log's posts, newest first, with what the History-style list
// shows of each.
import { type CollectionEntry, getCollection } from "astro:content";

export type Post = CollectionEntry<"blog">;

export async function posts(): Promise<Post[]> {
  return (await getCollection("blog", ({ data }) => !data.draft)).sort(
    (a, b) => b.data.date.valueOf() - a.data.date.valueOf(),
  );
}

/**
 * How long a post takes to read, as the deck's History writes how long a
 * track played: "7m". Counted from its text, without its imports and tags.
 */
export function readingTime(post: Post): string {
  const text = (post.body ?? "")
    .replace(/^import .*$/gm, " ")
    .replace(/<[^>]*>/g, " ")
    .replace(/[{}()[\]#*_`>|-]/g, " ");
  const words = text.split(/\s+/).filter(Boolean).length;
  return `${String(Math.max(1, Math.round(words / 200)))}m`;
}
