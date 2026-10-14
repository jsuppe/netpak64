# NetPak64 public showcase

This directory is the publication surface for the project. `index.html` is the
project overview; `web/` remains the deeper architecture documentation.

## Publishing requirements

- Explain the original objective, architecture, verified achievements, current
  limitations, and next milestones.
- Lead with gameplay evidence and label older development footage honestly.
- Link public source repositories by responsibility.
- Do not distribute ROM images, extracted Nintendo assets, credentials, private
  hostnames, or private network addresses.
- State clearly that the physical SummerCart64 path is not yet end-to-end.
- Keep measured results tied to their recorded development campaign rather than
  implying continuous production reliability.

## Media

Large MP4s are attached to the GitHub release tagged `showcase-2026-10-12`.
The static site keeps only optimized poster images in `assets/media/`.

- `netpak64-four-player-battle.mp4` — current four-instance gameplay capture.
- `netpak64-v76-native-ai-race.mp4` — archived v76 footage; premature one-lap
  finish/cinematic behavior makes it unsuitable as a clean gameplay example.
- Release `showcase-v77`: current 30-fps host-view and 15-fps four-peer excerpts,
  each with an uncut capture. Three full three-lap runs completed all 12
  participant finishes, with 9,241 / 7,664 / 8,040 compared frames and no divergence.
  Qualification report: https://github.com/jsuppe/mk64-online/blob/main/docs/QA-V77.md
- `netpak64-two-player-lockstep.mp4` — early input-overlay lockstep proof.
- `netpak64-lobby-flow.mp4` — host/find/join flow demonstration.

## Local preview

```sh
cd docs
python3 -m http.server 8080
```

Open `http://127.0.0.1:8080/`.
