# Kintsugi

*Imperfection, carefully restored.* A Reluctant Astronaut plugin, restored by OBP.

## What is Kintsugi?

Kintsugi (金継ぎ) is the Japanese art of mending broken pottery with gold. The cracks aren't hidden; they become the most beautiful part of the object, a record of everything it has lived through.

Kintsugi the plugin brings that idea to sound. It's an analog channel and bus processor (VST3 and AU) that gives digital tracks the life of an old console, a valve and a reel-to-reel machine: the weight of a transformer, the warmth of a valve breathing with your playing, the gentle wobble and softened top of aged tape, and a slow, living instability that keeps a mix from ever sitting perfectly still.

It isn't about damage or distortion. At its defaults it's felt rather than heard, and every stage is level-matched, so you judge colour, not loudness. Put it on every track and each becomes a slightly different channel on an imaginary 24-channel desk; put one more on the master and let Glue pull it all together. Pushed, it can be overdone, but its purpose is subtler: to make a track feel alive, filled with feeling and imperfect beauty.

On screen it is a piece of gear with a history: a worn green plate set in a broken stoneware case mended with gold, brass repairs, mismatched replacement knobs and the restorer's stamp. Elegant, a little broken, calm. A tool that invites you to listen, think and feel.

**In short:** Kintsugi gives digital tracks the warmth, movement and imperfect beauty of a restored analog desk, valve and tape machine: subtle by default, honest in level, alive in feel.

## How to use it like a summing mixer

A DAW mixes inside the computer, so no plugin can stand in for the summing itself. What gives an analog desk its sound is that **every channel** passes through slightly imperfect electronics before the mix bus, and then the bus itself adds its own glue. Kintsugi recreates that:

1. Put Kintsugi on **every track** (or every group), with Glue at 0. Each new instance takes the next channel on an imaginary 24-channel desk (see *Channel*), so no two tracks are coloured in exactly the same way.
2. Put one more on the **master bus** and turn up **Glue**.

Kintsugi is level-matched: at 0 VU (‑18 dBFS RMS), turning any drive or saturation knob changes the *colour*, not the loudness, so you can judge it honestly. 

## What it does and doesn't do

Cubase does the summing itself, digitally and perfectly cleanly. Kintsugi instances don't talk to each other. What you get is the analog *character* of a desk: every channel coloured slightly differently on its way in, then the bus amplifier and Glue working on the combined mix.

## The case

In the header, 金継ぎ (kintsugi) is printed beneath the name, in the same cream ink as the rest of the panel. The glyphs come from Noto Serif CJK JP (SIL Open Font License), embedded as outlines.


The plate sits in a glazed stoneware case, deep green-blue and stone-rough. It broke, and was mended the kintsugi way: the cracks filled with gold, small chips rebuilt in gold, nothing hidden. The other cases tried along the way (cherry, walnut, teak and steel, tolex, hammertone) are still in the code; `defaultCaseStyle` in PluginEditor.h picks one.

## Signal flow

Input → **Console** (hum, line amp, output transformer) → **Valve** → **Tape** (record head, transport, playback head) → Glue → Mix → Output.

Everything that saturates runs at 4× oversampling, so even heavy drive stays free of digital grit. Every drive knob eases off a little in its top half, so full tilt is obviously overdone but its harmonics still fall away smoothly: thick and saturated, never square-wave fizz. The tape transport also runs at 4×, so wow and flutter never dull the top end.

## Controls

Each of **Console**, **Valve** and **Tape** has a toggle switch beside its jewel lamp: lever up and the section is in (the lamp lights), lever down and it's out (the lamp goes dark and its knobs grey out). All three are in by default. The Valve lamp glows brighter the harder the tube is working.

**Console**
- **Drive:** how hard you hit the channel's line amplifier (‑6 to +24 dB in, compensated on the way out). Firm but rounded, mostly odd harmonics. Like all the drive knobs, the lower half is subtle colour; the top quarter is where it gets obvious.
- **Transformer (NICKEL / STEEL / IRON):** the core material of the channel's output transformer; the window under the switch shows which. Its saturation is driven by the line amp, so you hear more of it the harder you turn **Drive**. Only the bass saturates (the core runs out before the highs do).
  - *Nickel:* fast and clean, extended top, barely any low bloom. Saturates only when pushed hard.
  - *Steel (default):* the classic console sound: a little low weight (about +0.5 dB), gentle even harmonics.
  - *Iron:* big vintage core: saturates earliest, fat low end (about +1 dB), softer top (about ‑2 dB at 20 kHz).
- **Hum:** 50 Hz mains hum with a hint of ground-loop buzz (off at 0, about ‑45 dBFS at max). Use it sparingly.
- **Channel (No. 1–24):** which channel of the desk this is, shown on a numbered drum in the little window under the knob. Each one has its own fixed component tolerances: tiny gain and balance offsets, slightly different drive, transformer and top-end response, and its own tape-head azimuth. New instances count upwards automatically, and you can change the number yourself.
- **Glue:** a gentle bus compressor (up to 3:1, soft knee, slow release, bass kept out of the detector), level-neutral at 0 VU. Off at 0, the default: leave it there on tracks and turn it up on the master. About 1 dB of gentle gain reduction at 35 %, heavy squash at full.

**Valve**
- **Drive:** how hard the tube is driven (the range depends on the valve), level-compensated. A barely driven tube is clean; its warm even harmonics grow as you turn it up. Near the top, the valve's own input capacitance (the Miller effect, strongest on the high-gain ECC83) softens cymbals and air before they reach the saturation, so a pushed valve gets thick and dark, never fizzy.
- **Bias (COLD–HOT):** the valve's operating point, as on real gear: how much current it draws at rest. Level-matched at every setting; the middle is neutral.
  - *HOT:* less headroom, so the valve saturates earlier and more softly and compresses more: warmer and thicker. On the ECC82 and ECC83 (single triodes) it also clips more lopsidedly, adding even harmonics.
  - *COLD:* more headroom: tighter, cleaner and more controlled, with fewer even harmonics on the triodes.
  - *EL84:* it runs push-pull, so it stays symmetrical at any bias (the two halves cancel the even harmonics). Hot moves it towards smooth, compressing class A. Very cold, its two halves hand over imperfectly: a faint crossover grain that colours quiet notes and decays most and all but vanishes on loud ones.
- **Sag:** power-supply sag (strongest with the EL84). After loud notes the supply droops and the tube runs out of headroom sooner, so it squeezes and blooms with the playing. Like a real supply, it can only droop so far (at most about 8 dB), so it breathes rather than breaking up.
- **Valve (ECC82 / ECC83 / EL84):** which valve sits in the socket. Each has its own gain, headroom and harmonic character:
  - *ECC82 (12AU7):* a low-gain twin triode, the kind used where a circuit needs to drive things cleanly. Lots of headroom: it stays clear and open through most of the Drive range, adding only a gentle, sweet second harmonic and a little roundness. Driven hard it thickens politely rather than breaking up. The subtlest choice; lovely on vocals, acoustic instruments and the mix bus.
  - *ECC83 (12AX7) (default):* the classic high-gain preamp triode, heard in countless guitar amps and studio preamps. It saturates readily with strong even harmonics, so it sounds warm, full and rich, with a soft, rounded edge to transients. The Bias knob has the most effect here. The all-round warm choice: bass, drums, keys, guitars.
  - *EL84:* a small power pentode, the output valve of amps like the Vox AC30, here run as a push-pull power stage. Push-pull cancels the even harmonics, so it's rounder and more symmetrical than the others, mostly odd harmonics, and it compresses and blooms as it's pushed rather than snapping into distortion. Power-supply sag lives in the power stage, so **Sag** has by far its strongest effect here: loud notes squeeze and the sound breathes with your playing. Clean at low Drive; thick and compressed when pushed. Lovely on guitars, drums and anything that should feel played into a real amp.
- **Tone (DARK–OPEN):** a low-pass after the tube, from 2 kHz up to fully open.
- **Mix:** parallel blend of the tube with the clean signal.

**Tape**
- **Speed (7½ / 15 / 30 ips):** sets the machine's character (the window under the switch shows the speed). 7½ ips: a big low-end head bump around 48 Hz, a softer top (16 kHz about ‑6 dB) and more wow and flutter. 15 ips (default): the classic, a gentle bump around 75 Hz and an open top, within about 0.5 dB to 18 kHz. 30 ips: tight and open, almost flat to 20 kHz, the steadiest transport. At every speed the lowest octave below the bump eases off gently (a shelf, about ‑2 dB at 25 Hz), as on a real playback head, without the phase shift that would make the Mix knob thin out the bass.
- **Saturation:** how hard you hit the tape (with record pre-emphasis, so the highs saturate a little earlier, as on real tape).
- **Wow:** slow, irregular pitch drift (a slightly uneven reel). Kept below about ±0.1 % at the default; at max it's seasick (about ±0.9 %).
- **Flutter:** fast, fine pitch wobble from the capstan, plus a little scrape flutter.
- **Age:** the age of the tape. As it gets older the top end fades and the stereo image softens slightly as the playback head's alignment wanders. Barely there at the default; at full, 16 kHz is down about 18 dB: dull, warm and a little smeared.

**Output**
- **Instability:** the unit's slow, living imperfection. Level, left/right balance, how hard each stage saturates, valve bias, tape top end and tape speed all wander slowly and independently, over 5–15 seconds, each instance on its own path. Below conscious hearing at the default; a slow, seasick wander at full. It sits in the Output section because it touches all three sections: the large brass-skirted knob with the gold-mended ivory cap, above the engraved brass plaque.
- **Mix:** dry/wet. The dry signal is delayed to line up exactly with the processed one, so blending never causes phasing.
- **Output:** a long-throw console fader (‑24 to +12 dB). Double-click it to return to 0 dB.

## Programs

Cubase lists these as factory presets: **Desk** (the defaults), **Clean Console**, **Warm Tracking**, **Quarter-Inch**, **Bus Glue**, **Pushed Pentode** (the EL84, driven into bloom), **Faded Memory**. Choosing one keeps the instance's channel number.

## Latency

Kintsugi reports about 10 ms of latency (the tape transport needs room for wow to swing both ways). Cubase compensates for it automatically. While recording through it live, enable *Constrain Delay Compensation* or bypass it.

## Building on GitHub (no tools needed on your Mac)

1. Create a new repository on github.com and upload everything in this folder. macOS hides the `.github` folder, so if it doesn't come along: in the repo click **Add file → Create new file**, name it `.github/workflows/build.yml`, and paste in the contents of that file (also included as `build.yml` in the project root for easy copying).
2. GitHub builds the plugin automatically (Actions tab, about 5–10 minutes).
3. Open the finished run and download **Kintsugi-mac** under Artifacts.

## Sharing it with friends

Each GitHub build also makes **Kintsugi-Mac-Installer** (under Artifacts): a zip with a standard Mac installer (`Kintsugi.pkg`) and a short *Read Me First*. Send that zip to anyone. They double-click the installer, and it puts the VST3 and the Audio Unit in the right folders for every music program.

Because the installer isn't signed with a paid Apple developer certificate, macOS will warn the first time; the Read Me explains the one-time *Open Anyway* step in System Settings → Privacy & Security. (With an Apple Developer account, the workflow can be extended to sign and notarize it so there's no warning at all.)

## Installing on your Mac

1. Unzip `Kintsugi-VST3.zip` and move `Kintsugi.vst3` to `~/Library/Audio/Plug-Ins/VST3/` (in Finder: Go → Go to Folder…).
2. Because the plugin isn't signed with an Apple developer certificate, macOS will block it the first time. Open Terminal and run:

   ```
   xattr -cr ~/Library/Audio/Plug-Ins/VST3/Kintsugi.vst3
   ```
3. In Cubase: Studio → VST Plug-in Manager → rescan. Kintsugi appears under Distortion (vendor "Reluctant Astronaut").

The AU version (`Kintsugi.component`, goes in `~/Library/Audio/Plug-Ins/Components/`) is for Logic and other AU hosts; Cubase only needs the VST3.

## Building locally (optional)

Requires Xcode and CMake:

```
cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release --target Kintsugi_VST3
```

Offline tests: add `-DKINTSUGI_TESTS=ON` and build/run the `KintsugiTests` target. It also has `snapshot out.png`, `bench` and `process in.wav out.wav [param=value …]` modes.

## Fonts

Old Standard TT (SIL Open Font License, see Resources/OFL.txt), embedded in the plugin.
