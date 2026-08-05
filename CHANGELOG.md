# v1.2.1

- Added Relaxed, Standard and Hardcore run modes to Endless. Relaxed lets you restart the zone, return to the outpost or end the run after dying; Hardcore disables saves.
- Reworked the Run Over screen with lifetime cash earned and spent, separate records for each run mode and new milestone messages.
- Endless records now show a trailing C when a custom weapon flew the run, as in `Furthest zone: 58 C`.
- The Endless seed screen now shows the selected mode's zone record, and High Scores gained a final Endless page listing all three, each erasable behind an Are You Sure? confirmation.
- Added Rear Gun Scale for one-player Arcade modes, letting extra lives power up the rear gun too.
- Added Unused Sprites, on by default, giving 21 weapons and sidekicks distinct shop icons.
- Opening Salvo now carries its boost into delayed secondary explosions from weapons such as Mega Pulse and bomb sidekicks.
- Improved crash and rollback-netcode troubleshooting with clearer reports and timestamped per-session logs in a dedicated `log` folder, so older reports are no longer overwritten.
- FluidSynth now finds SoundFonts beside the game as well as in the `data` folder, and is unavailable when no SoundFont is present.
- Fixed the Endless E-Shop unexpectedly refreshing when moving between the Episode 1-3 and Episode 4-5 weapon sets.
