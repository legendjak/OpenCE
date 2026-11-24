# Custom build 387

This branch (`custom-387`) is OpenCE build 387 (tag `build-387`, `307a42e`)
with the pull requests below. Each pull request's own commits were taken
with `git cherry-pick -x`, not its branch: most were opened against
`testing`, whose 18 commits after build 387 are **not** in this build. Pull
requests whose branches merged another one were applied as one commit of
their net change (#635, #729, #733, #735, #737).

The build is network version **1045**, a number of its own: it plays only
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

## Not in the build

| Pull request | Why |
| --- | --- |
| #534 #536 #538 #540 #541 #542 #545 #546 #547 #548 #552 | merged into build 387 already |
| #577 #580 #593 #608 #613 #639 #720 #730 #731 #732 #734 #736 #739 | carried by a later pull request of the same branch (above) |
| #609 #727 #747 #771 | replaced by a newer version (#656, #773, #766, #772) |
| #709 #713 #714 #715 #717 #718 #722 #723 | replaced by #744, which takes them with fixes from their review |
| #742 | #745 fixes the same shot as an option; the maintainers preferred it |
| #598 | build 387's 1.0.5 hotfix draws the overshield's layers green and red already |
