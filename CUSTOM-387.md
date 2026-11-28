# Custom build 387

This branch (`custom-387`) is OpenCE build 387 (tag `build-387`, `307a42e`)
with the pull requests below. Each pull request's own commits were taken
with `git cherry-pick -x`, not its branch: most were opened against
`testing`, whose 18 commits after build 387 are **not** in this build. Pull
requests whose branches merged another one were applied as one commit of
their net change (#635, #729, #733, #735, #737).

The build is network version **1049**, a number of its own: it plays only
with copies of itself (`port/linux/NETCODE.md`). Saved games of build 387
still load: nothing here adds to the game state's allocations.

Build it as CI does: `python tools/ci_build.py windows release` (portable,
full LTO, PGO), which collects `dist/halo-windows-release`.

## In the build

| Pull request | Taken as | Fixed or changed here |
| --- | --- | --- |
| #544 video mask across a wide screen | itself | |
| #554 casings drawn in the world | itself | |
| #557 cloak's screen places linear | itself | its test stubs the newer `mcc_environment_shader_emit` |
| #558 cloaked legs and weapon apart | itself | |
| #559 cloaked first-person depth | itself | |
| #573 declare `render_camera_world_to_screen` | itself | |
| #574 race flags' numbers | itself | network version (below) |
| #575 a dedicated server's machines per address | itself | |
| #576 a swarm's spawned actors | itself | |
| #578 flashlight meter's fill | itself | |
| #581 heat meter while charging | itself | |
| #582 crosshair warnings and sniper caption | itself (it carries #577) | |
| #583 glow trailing particles | itself | |
| #584 HDR back buffer alpha | itself | |
| #588 rcp/rsq of zero | itself | |
| #595 no time code over endings | itself | |
| #596 rocket launcher warnings | itself | |
| #599 needler reload needles | itself | |
| #600 plasma pistol gauge heat | itself | |
| #601 atmospheric fog settles | itself | |
| #602 flashlight click in multiplayer | itself | |
| #603 powerup glass first | itself | the plane order test takes its function |
| #611 wedged items at rest | itself | |
| #614 cryotube interior | itself | |
| #633 overshield's force field | itself | network version; Xbox maps only |
| #634 second weapon to the action button | itself | network version |
| #635 body set back in a mirror | squashed (it carries #580) | merged with #774's shadow |
| #636 device render spheres | itself | |
| #656 Keyes's lip | itself (newer #609) | Xbox maps only |
| #657 marines' fingertips | itself | |
| #666 declarations | itself | |
| #744 (chestahh) | itself, in place of #709 #713 #714 #715 #717 #718 #722 #723 | merged with #766 and #772 |
| #710 build number setting | itself | its variable is `HALO_SHOW_BUILD_NUMBER` |
| #711 keyboard B and A | itself | stock background only (pixels' crc), tags found once |
| #712 lens flare reflection dimming | itself | size as drawn, nothing dimmed at 480 lines |
| #716 view changing hands is a cut | itself (it carries #593) | a handover before the tick's first frame |
| #719 sun glow targets' scale | itself | |
| #721 marker lights from the drawn pose | itself | objects choose lights from where they are drawn |
| #724 command-list actors | itself (it carries #720) | |
| #725 no blend across pose spaces | itself | undrawn after a checkpoint load; README |
| #726 a dead unit's line | itself | |
| #728 scaled objects' lights | itself | drawn radius only, and only smaller |
| #729 fades, bars and titles between ticks | squashed (it carries #730, #731) | merged with #745's bars |
| #733 a10 hangar | squashed (it carries #639, #732) | |
| #735 a10 cryo steam | squashed (it carries #613, #734) | |
| #737 Johnson's placement | squashed (it carries #608, #736) | |
| #738 Cortana's chip | itself | |
| #740 Pelican lights, Longsword exhaust | itself (it carries #739) | Xbox maps only |
| #741 crewmen's hands | itself | Xbox maps only |
| #745 Xbox cutscene framing | itself, in place of #742 | its menu row's label fixed when merged |
| #748 return to the menu after the campaign | itself | |
| #756 loading screen held | itself | |
| #757 widescreen loading picture | itself | |
| #758 menu files on Windows | itself | |
| #759 Bink movies | itself | |
| #760 attract mode with the PC menus | itself | |
| #766 (Zack117-dot) reload rounds | itself, in place of #747 | merged with #744's first-person cut |
| #767 (Zack117-dot) no frame before the first tick | itself | |
| #772 (legendjak) readied weapons | itself, in place of #771 | merged with #744's cuts; its test brought up to date |
| #773 (legendjak) pickup icons, no stutter | itself, in place of #727 | sharpened in bands: 22 MiB at most, not 64 |
| #774 (legendjak) first-person shadow | itself | merged with #635's mirror |
| #775 (legendjak) carry weapons | itself | |
| #708 (GDAndres98) contrail crashes | itself, beside #776 | merged with #776's head |
| #776 (legendjak) contrail head | itself | |

Also fixed here, in pull requests build 387 already has: #540 (the z bias
in the fixed-function program too) and #552 (the lightning seeds forgotten
after a load), as their reviews asked.

## Added for this build

- Retail Halo PC's maps (cache version 7) run as Custom Edition maps do,
  from `paths.custom_edition` (for example `D:\Games`).
- Halo PC's rocket warthog, Banshee, Shade, flamethrower and fuel rod gun on the
  game's own multiplayer maps (`game.gearbox_items`, on): taken with their
  placements from the Halo PC map of the same name in that install, at load
  (`port/linux/game/gearbox_items.c`). The gametype's vehicle options place
  the rocket warthogs and Banshees (PC, or counts; the default set places the
  Xbox's vehicles alone); the weapons follow the weapon set (Classic leaves
  them out) and can be a custom loadout's. The Xbox's players could not hold
  the fuel rod gun, so Halo PC's player animations for it are added to the
  Xbox map's (its own are kept), and its pickup message to the HUD's. This
  adds tags to a map at load,
  which the project's rules do not allow upstream: it is this build's alone.
  A network game records whether its map has them (network version 1049), and
  a machine that differs from the host is told why it cannot join. Their HUD
  is drawn with the high-res textures and upscaling of Halo Custom Edition's
  stock sheets (Halo PC's are the same, checked by their pixels), and the
  two reticles Halo PC added (the fuel rod gun's and the rocket warthog's)
  are redrawn in vectors (`tools/pc_reticles.py`). The Xbox's maps were not
  made for flying: a Banshee is held by the invisible walls and lower ceiling
  of the Halo PC map of the same name (its collision read from that map's
  file), and put back where it last was over the map's ground if it leaves
  it.
- Halo PC's own maps get the high-res HUD too: their tags are all Halo PC's,
  so each sheet counts as stock, its pixels checked as Custom Edition's are.
- The carnage report's B and A are drawn at the menus' size, beside their
  words, and Halo PC's maps' "ESCAPE = Quit    ENTER = Continue" shows the
  buttons unless the keyboard's prompts are on; the line is moved left when
  it would run off the edge.
- Gearbox's changes to Blood Gulch, Battle Creek and Sidewinder are made to
  the Xbox's maps (`game.gearbox_map_changes`, on, with the items). Battle
  Creek and Blood Gulch get the Halo PC map's structure BSP, read beside
  them at its own address above the Halo PC map's tags as a Halo PC map's
  is, its references made the Xbox map's tags of their names (Halo PC's
  added where the Xbox map has none, its lightmaps always), and loaded in
  place of the Xbox map's: Battle Creek's side windows are smaller and
  cannot be jumped through, and a small cliff on Blood Gulch that could not
  be walked up can be. Sidewinder keeps its own: Halo PC's (thick pillars in
  the middle tunnel, rooms for the teleporters) has not the shadows of its
  trees and cliffs. Its arrows on the floor to the teleporters are drawn on
  the Xbox's as decals: copies of Sidewinder's stripe decal with Halo PC's
  arrow bitmaps, added to the tag table, and placed in the clusters of the
  floor under them in the structure BSP's runtime decals as it loads. And
  some of the Halo PC map's placements
  stand for the Xbox map's: Blood Gulch's shrub that floated set on the
  ground and another beside it, Sidewinder's teleporters in its bases (with
  their flags), red and blue beacons, and two of each power-up in the middle
  tunnel. The map's version says whether it has them. This changes three
  maps by name, takes tags and a BSP from another map, and makes tags,
  which the project's rules do not allow upstream
  (`port/linux/game/gearbox_items.c`).
- Halo PC's crosshair warnings ("Reload", the rocket warthog's dots) are drawn
  with the Xbox's high-res texture: the sheets are laid out alike.
- Fixed the far half of a map flickering out and in while moving (Halo PC's
  Timberland, looking over a hill): the region of the screen seen through
  each portal is widened by about two pixels before the next portal is
  clipped by it, so the slivers seen along a row of portals at a slant no
  longer come and go a millimetre apart (`source/structures/structure_visibility.c`).
- Halo PC's maps' teleporters show the Xbox's swirling field rather than Halo
  PC's flat green one (`display.xbox_teleporters`, on): the Xbox's shader and
  bitmaps of that name are read from one of the game's own multiplayer maps
  and stand in for Halo PC's (`port/linux/game/xbox_shaders.c`).
- The Covenant field generators' shields (Danger Canyon, Gephyrophobia) and
  Gephyrophobia's bridge's energy ropes look as the Xbox's campaign draws
  them (`display.xbox_campaign_shaders`, on): a see-through violet field
  with moving streaks that bends what is behind it, where Halo PC draws a
  white-rimmed bubble of blue dust; and bright animated blue ropes, where
  Halo PC's are faint lines. Halo PC has not the Xbox's generic
  transparent shaders; Gearbox drew simpler ones in their place (the
  ropes' a copy of b40's, with b40's bitmaps). The Xbox's are read from
  the game's own campaign maps (the shield from c20, the ropes from b40's
  `b40_energyrope`), with their bitmaps, as the teleporters' are, and
  stand in for Halo PC's (`port/linux/game/xbox_shaders.c`). A map is
  read in pieces as it is inflated, so that b40 (280 MB inflated) is never
  held whole: the first Halo PC map of a run with them takes about 2
  seconds longer to load. This picks tags by name, Gephyrophobia's ropes'
  among them.
- The floor at the teleporters of Halo PC's own maps glows green, as the
  Xbox's maps' lightmaps make it glow around theirs (`display.teleporter_glow`,
  on): a green dynamic light just over the floor at each teleporter's
  entrance, moved out from the frame and the wall behind it toward the open
  side, with no highlight (`port/linux/game/teleporter_glow.c`). Gearbox's
  lightmaps of Danger Canyon, Death Island, Ice Fields, Infinity and
  Gephyrophobia had little or none of it; Timberland has no teleporters.
- Halo PC's multiplayer Shade's bolts look like the Xbox's campaign Shade's
  (`display.campaign_shade`, on): its contrail gets the campaign's 30 points a
  second (Halo PC's has 10) and three narrow white states, its firing effect
  the campaign's brighter flashes, counts and last flash, its smoke and its
  light the campaign's sizes. The values are the Xbox's b30's, set at load on
  the four tags only while they hold Gearbox's (checked), and given back as
  the map unloads (`port/linux/game/campaign_shade.c`). The bolts still fly
  and hurt as Halo PC's (60 world units a second, the campaign's 25), so the
  trail is longer. Like the items, this picks tags by name, which the
  project's rules do not allow upstream.
- Getting into and out of a vehicle, the view goes over your head, not
  through it (`display.vehicle_camera`, on). The game moves the view in a
  line from your eyes to behind the vehicle and back: getting in, the
  picture was the inside of your armor for a quarter of a second; getting
  out, the back of your helmet filled it, and the view jumped the last
  quarter of a world unit into your eyes. Now the view drawn is kept a
  little over your body near it, so your body comes into the picture from
  below as the view leaves it and goes out of it below as the view comes
  back; coming back, it glides the last part into your eyes; and your body
  is not drawn while the view is inside it
  (`port/linux/game/vehicle_camera.c`). Only the picture changes: the
  game's camera, which aim assistance and the sounds take, is as it was.
- Halo PC's fuel rod gun is glossy purple in first person, as Gearbox's E3
  2003 trailer shows it (`display.e3_fuel_rod`, on). Its first-person
  texture's masks have only a reflection mask, kept in the channel Halo PC
  reads the color change mask from, so Halo PC draws the gun without the
  Banshee's purple reflections and the detail its shader gives it: dark
  blue-grey. Here that mask is read as the reflection mask, if the texture
  is as Gearbox made it (one 16-bit bitmap, its color channels empty,
  checked as it loads) (`port/linux/game/custom_edition_bitmaps.c`). This
  picks a tag by name too.
- Halo PC's six maps of its own (Ice Fields, Death Island, Danger Canyon,
  Infinity, Timberland, Gephyrophobia), found in `paths.custom_edition`'s
  maps, are listed under MULTIPLAYER after the Xbox's maps, with Halo PC's
  pictures (read from its `ui.map` and `bitmaps.map`) and descriptions,
  rather than under CUSTOM MULTIPLAYER.
- A controller's player sees "Hold [BACK] for score" with the button drawn as
  a cap (BACK, VIEW, SHARE, CREATE or -, by the controller), as a keyboard's
  player sees its key: the HUD's sheets have no art for that button.
- A weapon's first- and third-person particle streams are drawn apart on a
  Halo PC or Custom Edition map, or one with Halo PC's items, as Halo PC
  draws them (MCC maps already were): the flamethrower's flames leave its
  nozzle in first person, rather than rising from the player's feet.
- The MULTIPLAYER menu's picture (SERVER BROWSER and LAN showed a
  placeholder) is red and blue Spartans with a flag, a picture its player
  gave (`port/assets/menus/NON_HANDDRAWN.md`). The project ships only
  hand-drawn redraws there, and `tools/ce_menus.py` writes the
  placeholders again.
- The menus' other pictures that were placeholders are Gearbox's own, read
  from Halo PC's `ui.map`: EDIT PROFILE SETTINGS' controls, mouse, audio,
  video, network and about pictures, and EDIT MULTIPLAYER GAMETYPE's change
  name, item, vehicle and teamplay options and its OK. They are 3D renders
  (`port/assets/menus/NON_HANDDRAWN.md`), which the project does not ship.
- Text chat in public internet games a player hosts (listed in the server
  browser), as the host's TEXT CHAT says (`network.public_text_chat`, on).
  The main build keeps such games without chat, on the host and on every
  player who joins from the server browser; here each machine with the
  setting on follows the host instead, so the host and the players both need
  it. `false` restores the main build's rule.

## Not in the build

| Pull request | Why |
| --- | --- |
| #534 #536 #538 #540 #541 #542 #545 #546 #547 #548 #552 | merged into build 387 already |
| #577 #580 #593 #608 #613 #639 #720 #730 #731 #732 #734 #736 #739 | carried by a later pull request of the same branch (above) |
| #609 #727 #747 #771 | replaced by a newer version (#656, #773, #766, #772) |
| #709 #713 #714 #715 #717 #718 #722 #723 | replaced by #744, which takes them with fixes from their review |
| #742 | #745 fixes the same shot as an option; the maintainers preferred it |
| #598 | build 387's 1.0.5 hotfix draws the overshield's layers green and red already |
