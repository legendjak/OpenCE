/*
XBOX_TELEPORTERS.H

The Xbox's teleporter field on Halo PC's maps (xbox_teleporters.c).
*/

#ifndef __XBOX_TELEPORTERS_H
#define __XBOX_TELEPORTERS_H

/* ---------- prototypes/XBOX_TELEPORTERS.C */

/* After a Halo PC or Custom Edition map's tags and the menus' have loaded:
its teleporter field drawn with the Xbox's shader, read from one of the
game's own maps (display.xbox_teleporters); and before the menus' tags go,
taken away. */
void xbox_teleporters_tags_loaded(
	void);
void xbox_teleporters_tags_unloaded(
	void);

/* A read (cache_file_read's) of the Xbox's bitmaps' pixels: served from
what was read, and TRUE; FALSE for any other tag's. */
boolean xbox_teleporters_read(
	long tag_index,
	long offset,
	long size,
	void *buffer);

#endif
