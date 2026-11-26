/*
CAMPAIGN_SHADE.H

The campaign Shade's look on Halo PC's multiplayer Shade (campaign_shade.c).
*/

#ifndef __CAMPAIGN_SHADE_H
#define __CAMPAIGN_SHADE_H
#pragma once

/* ---------- prototypes/CAMPAIGN_SHADE.C */

/* after a map's tags load (and Halo PC's items are added to an Xbox map's:
gearbox_items.c), before anything is drawn with them */
void campaign_shade_tags_loaded(
	void);
/* as they unload, before they go: what was changed given back */
void campaign_shade_tags_unloaded(
	void);

#endif // __CAMPAIGN_SHADE_H
