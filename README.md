# Taipan for PocketBook Era Color

A touch-screen port of the classic trading game Taipan (set in the 19th-century South China Seas) for the PocketBook Era Color e-reader (7" Kaleido 3 color e-ink, 1264x1680).

Based on the rules of the Unix/ncurses version 0.9 of the Apple ][ game.

- Created by Art Canfil
- Programmed by Jay Link
- Apple ][ program coded by Ronald J. Berg
- Copyright (c) 1978 - 2002 Art Canfil

This is an unofficial fan port. The original source is not included here.

## Layout

- `core/`: portable C++11 game rules as an event-driven state machine (no UI), with unit tests and a random-playthrough fuzz test (`core/test.sh`, run in Docker).
- `ui/`: thin PocketBook InkView touch UI (`main.cpp`), built with the PocketBook SDK.

## Tests

```
docker run --rm -v "<path to core>:/src:ro" ubuntu:22.04 sh /src/test.sh
```

## Build

Needs the PocketBook SDK (B300-6.8, not included; see https://github.com/pocketbook/SDK_6.3.0, release 6.8) extracted into a Docker volume mounted at `/BUILD`. Then run `ui/build.sh` in an ubuntu:22.04 container with the project mounted at `/src`. Copy the resulting `ui/taipan.app` to the reader's `applications` folder.

## Differences from the original

- Storms can sink a badly damaged ship; seizure fines cannot exceed your cash; large amounts are formatted correctly.
- Orders are chosen each battle round; there is no battle animation.
- The game autosaves while you are in port.
