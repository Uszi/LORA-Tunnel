# Recon detection sounds

Current source uses the same short original alert motifs on Cardputer ADV and
Heltec. The Cardputer plays through its built-in speaker; Heltec uses its
buzzer. Sound is optional and is permitted only during an active Recon capture.
Wardriver, a stopped session and merely viewing the Recon page remain silent.
Stop, mute, changing capture mode and shutdown cancel the current motif.

## One cue per observed device identity

An eligible identity gets one short motif during a Recon session. Continued
sightings, changing RSSI, waiting 30 seconds, muting/unmuting, or eviction from
the display summary do not replay it. Starting an explicit new Recon session
resets this history. The existing category filters, confidence threshold and
winning rule's `alert:false` still determine whether a detection is eligible.
The global one-second admission spacing remains; there is no repeating alarm.

The history holds 128 non-evicting 64-bit fingerprints in exactly 1 KiB. It keys
on radio kind, address type and normalized observed address. Once full, new
sounds are suppressed and status reports the full history; capture/logging
continue. It never evicts an older identity just to replay its sound. A device
that rotates addresses may appear as a new identity, and a fingerprint collision
can suppress a cue; this is bounded notification bookkeeping, not proof of a
physical device's identity.

No sound queue grows behind the user interface. While a motif is active, an
equal- or lower-priority request is rejected and remains eligible for a later
sighting; a higher-priority motif can interrupt it. An interrupted identity is
still remembered and is not replayed. Admission priority is an audio scheduling
choice, not a threat score or stronger evidence of identity.

## Original sound palette

These are newly composed, short retro game-style riffs and electronic chirps,
not transcriptions of game or movie melodies. Each is under one second of
nominal note time, with a 1.2-second scheduler cutoff for delayed loop ticks.
Pauses and brief gaps between notes keep individual cues distinct.

| Existing classification | Cue character |
| --- | --- |
| ALPR vendor candidate | Quick high/low robot chirps |
| Flock/accessory candidate | A different syncopated robot-chirp sequence |
| Body-camera category with explicit Axon/TASER evidence | Short alternating siren-like tones |
| Other body-camera candidates | Neutral two-register camera riff |
| iBeacon | Short three-note beacon chime |
| Ring vendor candidate | Descending door-chime-style phrase |
| Camera vendor candidate | Compact rising camera riff |
| Drone/Remote ID | Rapid ascending/descending flight arpeggio |
| Find My/AirTag candidate | Separated high tracker chirps |
| Other tracker candidates | A short tracker motif; existing Samsung/SmartTag, Tile and Find Hub evidence selects distinct variants |
| Camera-glasses candidate | Bright ascending retro phrase |
| Acoustic-sensor candidate | Low/high response motif |
| Serial-module candidate | Short stepped electronic phrase |
| Radio-tool candidate | Original retro power-up-style riff; existing Flipper, Pwnagotchi peer or Biscuit evidence selects a distinct variant |
| Custom rule candidate | Neutral ascending three-note phrase |

Brand variants inspect only bounded classification evidence/vendor fields
within the matching category. The sound does not authenticate an observed name,
manufacturer, product, operator, recording state or intent. For example, a
siren-like cue is still a candidate classification, not a police identification.

## Implementation and validation limits

The shared scheduler and RTTTL reader allocate no memory, wait on no task and
perform no sleeps. They run from the main loop, outside radio callbacks. Notes
have finite hardware durations; the loop advances at most one note per tick,
so a late tick cannot replay a burst. Hardware tone output must also remain
non-blocking and stop immediately when requested.

The intentionally bounded RTTTL subset uses explicit `d`, `o` and `b` defaults,
denominators 1/2/4/8/16/32, octaves 4–7, tempo 60–240, notes/rests, supported
sharps and one dotted duration. Input is at most 192 bytes and 24 notes, and the
whole pattern is validated before playback. Runtime loading of arbitrary songs
or user files is not provided.

The actual production implementation passed 14 native tests covering all
patterns, malformed input, timer wraparound, stalled ticks, preemption, mute and
cancel, no replay, full-history suppression, display-cache churn and zero C++
allocations. These are software checks. Check actual loudness, intelligibility,
silence in Wardriver/idle, no repeats, mute/Stop/sleep cancellation and capture
responsiveness on both boards before claiming hardware acceptance. Host WAV
previews, when supplied, are synthetic reference audio rather than recordings
of either board's speaker.
