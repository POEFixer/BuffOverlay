# Changelog

## 1.0.4

- Follows the program language (ten languages).
- A long text typed in another alphabet (Cyrillic, Thai, Chinese) is no longer cut in the middle of a letter.

## 1.0.3

- Works with the separate menu window (POEFixer 366): the host hands the plugin the right drawing context for its settings tab and for its overlay.
- With the menu in a separate window, an icon dropped from the settings preview is freed only after both windows have drawn it (before, this could crash).
- "Add tracker" and "Center on screen" place the tracker by the game's screen size, not by the size of a separate menu window.

## 1.0.2

- Rebuilt with the updated plugin SDK headers (POEFixer 365 adds optional calls for UI control names, stash objects and tabs, wider entity scans and the host automation flag). No change in what the plugin does.

## 1.0.1

- Diagnostics now looks the same as in the other POEFixer plugins.
- The bottom of the settings tab names the plugin's original author, as in the other plugins maintained by POEFixer.

## 1.0.0

- First POEFixer release of Buff Overlay, with your trackers from the original plugin imported automatically.
- Much lighter on the game: buffs and skills are read 20 times a second instead of every frame, and only when a tracker needs them.
- Pick a buff or skill from what is active on your character; names ending in a changing number get a "Starts with" rule.
- New settings layout with a live preview, plain choices for icon, text and time left, and tooltips on every option.
- "Arrange on screen" lets you drag trackers with the menu open or closed and is no longer left on after a restart.
- Text outline and overall opacity for better readability; time left above a minute shows as minutes:seconds.
- Icons are chosen from the plugin's icons folder inside the overlay (no more blocking file dialog).
- Settings save automatically and safely; removing every tracker no longer brings the example back.
