# Buff Overlay

Shows the buffs and skill states you care about as large, readable icons and
text anywhere on your screen: charges and stacks, time left, or a word of your
own, for example "BOND 12.4s" next to your character while Ancestral Bond is up.

## Using it

1. Install **Buff Overlay** from the Plugin Manager (the first tab of the
   Plugins window), then tick **Enabled** on its tab. Updates are offered in
   the Plugin Manager too.
2. In **Trackers**, select the example tracker or press **Add tracker**.
3. In **What to watch**, press **Pick** to choose from the buffs (or skills) that
   are active on your character right now, or type the name yourself.
4. In **What to show**, choose icon, text and time left; the preview shows the
   result live.
5. Tick **Arrange on screen** and drag the tracker where you want it. Press
   **Done** at the top of the screen when finished (this also works with the
   POEFixer menu closed).

Settings are saved automatically about a second after each change.

## Settings

### Overlay
- **Arrange on screen**: shows every enabled tracker with a frame you can drag,
  even when its buff is not active. Not remembered after a restart.
- **Text outline**: a thin dark outline that keeps text readable on bright and
  dark parts of the map.
- **Opacity**: how see-through all trackers are.

### Trackers
The list of trackers. The tick box turns a tracker on or off; the dot is green
while the tracker matches something. **Add tracker**, **Duplicate** and
**Remove** manage the list (up to 64 trackers).

### What to watch
- **Tracker name**: your own name for the tracker.
- **Watch**: a buff on your character, or one of your active skills.
- **Name to find**: the game's internal name of the buff or skill. **Pick** lists
  what is active now; a buff name ending in a number (such as `shrine_cold_2`,
  Freezing Shrine) is turned into a "Starts with" rule automatically. POEFixer
  375 and older showed every buff name with a stray `_79` ending; names saved
  from those lists keep finding the same buff.
- **Match**: exact name, starts with, or contains (case-sensitive).
- **Skill value** (skills only): use stage, total uses, available charges, max
  uses, or ready to use.
- **Show when** and **Compare with** (skills only): the tracker counts as found
  only while the skill value meets this condition.
- **Several matches** (buffs only): add up the charges of every matching buff,
  or show the first one only.
- **When not found**: hide the tracker, or show it with 0.

### What to show
- **Icon**: a picture before the text. Put PNG, JPG or BMP files (up to
  1024 x 1024) into the plugin's `icons` folder and pick them with **Choose**,
  or enter a full path. **Reload** reads a replaced file again.
- **Text**: none, the value (charges, stacks or the skill value), or your own
  custom text.
- **Time left** (buffs only): seconds until the buff ends (minutes:seconds above
  a minute); buffs without a duration show an infinity sign.
- **Icon size** and a live **Preview**.

### Look and position
- **Text size**, **Text colour**, and an optional rounded **Background** behind
  the text.
- **Position**: the top-left corner in screen pixels. **Center on screen** moves
  the tracker to the middle of the screen.

### Diagnostics
Collapsed by default. Lists every buff and active skill the plugin can see on
your character (click a name to copy it) and opens the settings folder.

## Known limits

- Buff and skill names are the game's internal names, not the names shown in
  the game. Use **Pick** or the Diagnostics lists to find them.
- Positions are screen pixels: after changing the game resolution, trackers
  outside the new screen are kept at its edge until you move them.
- Active skills have no duration, so time left is available for buffs only.

## Settings file

`config/settings.json` in the plugin folder. Trackers from the original plugin's
`config/settings.txt` are imported automatically the first time.

## Credits

Originally created by 1k4ru5g3 (https://github.com/1k4ru5g3/BuffOverlayPlugin).
Maintained by POEFixer since October 2026.
