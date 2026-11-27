/*
XBOX_SHADERS.H

The Xbox's shaders on Halo PC's maps (xbox_shaders.c).
*/

#ifndef __XBOX_SHADERS_H
#define __XBOX_SHADERS_H

/* ---------- prototypes/XBOX_SHADERS.C */

/* After a Halo PC or Custom Edition map's tags and the menus' have loaded:
its teleporter field, Covenant field generators' shields and energy ropes
drawn with the Xbox's shaders, read from the game's own maps
(display.xbox_teleporters, display.xbox_campaign_shaders); and before the
menus' tags go, taken away. */
void xbox_shaders_tags_loaded(
	void);
void xbox_shaders_tags_unloaded(
	void);

/* A read (cache_file_read's) of the Xbox's shaders' bitmaps' pixels: served
from what was read, and TRUE; FALSE for any other tag's. */
boolean xbox_shaders_read(
	long tag_index,
	long offset,
	long size,
	void *buffer);

#endif
