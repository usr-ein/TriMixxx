# prolinks-compat

## Mission

Research, document, and ultimately implement compatibility with the **Pioneer
CDJ ProLink protocol** — the proprietary Ethernet protocol that lets multiple
Pioneer DJ players (CDJs) and mixers discover each other on a network and share
access to each other's music libraries (linked playback).

This work supports **TriMiXxX**, a custom DJ unit that behaves like a CDJ. It is
built on a Raspberry Pi running the open-source **Mixxx** software. A USB drive is
plugged into the TriMiXxX unit; Mixxx automounts it and plays its tracks. The goal
is to extend Mixxx so that, over the same Ethernet network as real CDJs, it can:

1. **See libraries from other CDJs** on the network (browse and load their tracks).
2. **Share its own library with other CDJs** on the network.

To other Pioneer hardware, TriMiXxX should appear as a **legitimate unit** —
announcing itself on the ProLink network, participating in device discovery, and
serving/consuming the relevant ProLink services so that real CDJs interoperate
with it without rejecting it.

## Approach (phased)

1. **Research (this repo's `research/` folder).** Study the existing open-source
   reverse-engineering literature on ProLink and write it up as a coherent,
   self-contained set of markdown documents that fully specify what we need to
   build. This is the current phase.
2. **Python proof-of-concept.** Build a standalone Python program that implements
   the two objectives above, to formalise a simple, working approach end-to-end
   and validate it against real hardware.
3. **Mixxx integration.** Port the validated approach into Mixxx (C++/Qt) and
   submit it upstream as a pull request.

## Test environment

- The author owns **two Pioneer CDJ-2000NXS** units and can test on a real
  ProLink network.
- A Mac + Python program + USB Ethernet dongle is the development/sniffing setup.
- New network captures (pcap) can be gathered on demand to fill research gaps.

## Reference projects (cloned into `research/ref-repos/`, git-ignored)

- evanpurkhiser/prolink-tools — TypeScript ProLink toolkit (overlays, metadata).
- evanpurkhiser/prolink-connect — TypeScript ProLink protocol library.
- grantHarris/prolink-cpp — C++ ProLink implementation.
- teknopaul/libcdj — C library + tools for the CDJ/DJ Link protocol.
- flesniak/python-prodj-link — Python ProDJ Link client.
- Deep-Symmetry/dysentery — the canonical reverse-engineering effort + protocol analysis.
- nzoschke/vizlink — Go ProLink visualizer.

## Conventions

- Research docs live in `research/` as numbered markdown files for ordering.
- Keep claims sourced: when a fact comes from a specific repo or capture, say so.
- Distinguish **confirmed** behaviour (verified against hardware / dysentery) from
  **inferred** behaviour.
