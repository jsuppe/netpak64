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
The static site keeps optimized posters and emulator screenshots in `assets/media/`; MP4s remain release assets.

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

## Setup ROM v0.4 / 16 October 2026

- `prototype.html` documents focused 3D menus, controls and the verified local
  ROM → ares PI management device → host HTTP worker → update-service path.
- Release `setup-v0.4` holds `netpak-setup-v0.4-3d-walkthrough.mp4`.
- Screenshot names begin `netpak-setup-v0.4-` in `assets/media/setup-v0.4/`.
  Visible captions identify the capture build, not the downloaded artifact’s
  separate `0.2.0-demo` version. Manifest preserves build/source/media digests.
- Required for new media: put the **source ROM/build version in the filename**
  and visible caption; never silently overwrite earlier-version evidence.
  Historical gameplay captures without proven build provenance must not be
  assigned a guessed ROM version.
- The page reports a dated local verification, not a live public status feed.
  Update HTTP is not carried through the gameplay relay. NIC/DHCP remain modeled;
  there is no installed firmware, signing or physical board qualification.
- Only emulator captures are published in this update; no ROM/firmware download.
