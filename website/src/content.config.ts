import { existsSync } from "node:fs";

import { defineCollection } from "astro:content";
import { glob } from "astro/loaders";
import { z } from "astro/zod";

// The build log: one MDX file per post, its pictures beside it.
const blog = defineCollection({
  loader: glob({ base: "./src/content/blog", pattern: "**/*.{md,mdx}" }),
  schema: ({ image }) =>
    z.object({
      title: z.string(),
      description: z.string(),
      date: z.coerce.date(),
      period: z.string(),
      tags: z.array(z.string()).default([]),
      cover: image(),
      coverAlt: z.string(),
      // A photo from the media repository to use instead, by id, when it is there.
      coverPhoto: z.string().optional(),
      draft: z.boolean().default(false),
    }),
});

// Photographs of the decks, from their own repository: the trimixxx/ folder of
// usr-ein/static-resources, cloned into ./media by `npm run media`. Each photo
// has a YAML file beside it. Without the clone the collection is empty and the
// site builds without photos.
const photosDir = "./media/trimixxx/photos";
const photos = defineCollection({
  loader: existsSync(photosDir) ? glob({ base: photosDir, pattern: "**/*.{yaml,yml}" }) : () => [],
  schema: ({ image }) =>
    z.object({
      image: image(),
      alt: z.string().min(1, "Every photo needs alt text"),
      caption: z.string().optional(),
      date: z.coerce.date(),
      tags: z.array(z.enum(["build", "deck", "event", "boards", "detail", "screen"])).default([]),
      credit: z.string().optional(),
      featured: z.boolean().default(false),
    }),
});

// Short silent clips, beside the photos: trimixxx/video/<id>.mp4, a poster
// frame <id>.jpg and <id>.yaml. The MP4 itself is emitted by the Clip
// component (import.meta.glob), the poster goes through the image pipeline.
const clipsDir = "./media/trimixxx/video";
const clips = defineCollection({
  loader: existsSync(clipsDir) ? glob({ base: clipsDir, pattern: "**/*.{yaml,yml}" }) : () => [],
  schema: ({ image }) =>
    z.object({
      video: z.string().regex(/^\.\/[\w.-]+\.mp4$/, "A clip's video is ./<name>.mp4"),
      poster: image(),
      label: z.string().min(1, "Every clip needs a label for screen readers"),
      caption: z.string().optional(),
      date: z.coerce.date(),
      tags: z.array(z.enum(["build", "deck", "event", "boards", "detail", "screen"])).default([]),
      credit: z.string().optional(),
    }),
});

export const collections = { blog, photos, clips };
