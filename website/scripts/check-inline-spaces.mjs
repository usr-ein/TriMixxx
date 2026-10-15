// Fails on a line break that Astro will silently turn into no space at all.
//
// Astro 7 compresses HTML with JSX whitespace rules, as the Prettier plugin
// formats it: a line break between text and an inline element is nothing, so
//   a fork of
//   <a href="https://mixxx.org">Mixxx</a>
// renders "a fork ofMixxx". Prettier writes {" "} where it wraps a line itself;
// a line break typed by hand needs one too. Run by `npm run lint`.
import { globSync, readFileSync } from "node:fs";

const inline = "a|abbr|b|code|em|i|kbd|span|strong|time";
const opensInline = new RegExp(`^\\s*<(${inline})\\b`);
const closesInline = new RegExp(`</(${inline})>\\s*$`);
const endsInText = /[\w,;:)’'"]$/;
const startsWithText = /^[\w(‘“]/;

let problems = 0;
for (const file of globSync("src/**/*.astro")) {
  const lines = readFileSync(file, "utf8").split("\n");
  const fences = lines.flatMap((line, i) => (line.trim() === "---" ? [i] : []));
  const start = fences.length >= 2 ? (fences[1] ?? 0) + 1 : 0;
  let inBlock = false;
  for (let i = start; i < lines.length - 1; i++) {
    const line = (lines[i] ?? "").trimEnd();
    const next = lines[i + 1] ?? "";
    if (/<(style|script)\b/.test(line)) inBlock = true;
    if (/<\/(style|script)>/.test(line)) inBlock = false;
    if (inBlock) continue;
    const textThenElement =
      endsInText.test(line) &&
      !line.endsWith('{" "}') &&
      !/^\s*[<{/*]/.test(line) &&
      opensInline.test(next);
    const elementThenText = closesInline.test(line) && startsWithText.test(next.trim());
    if (textThenElement || elementThenText) {
      problems++;
      process.stderr.write(
        `${file}:${String(i + 1)}: the line break here renders as no space; end the line with {" "}\n`,
      );
    }
  }
}
if (problems > 0) process.exit(1);
