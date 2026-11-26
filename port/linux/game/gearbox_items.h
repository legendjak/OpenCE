/*
GEARBOX_ITEMS.H

Halo PC's multiplayer additions on the Xbox game's multiplayer maps: the
rocket warthog, the multiplayer Banshee, the flamethrower and the fuel rod
gun, taken with their placements from the Halo PC version of the same map
(gearbox_items.c).
*/

#ifndef __GEARBOX_ITEMS_H
#define __GEARBOX_ITEMS_H

/* ---------- constants */

/* what a Halo PC bitmap the items bring beside the Xbox map's of its name is
called: that name and this */
#define GEARBOX_ITEMS_OWN_NAME_SUFFIX " (halo pc)"

enum
{
	_gearbox_item_flamethrower = 0,
	_gearbox_item_fuel_rod,
	NUMBER_OF_GEARBOX_WEAPONS
};

/* ---------- prototypes/GEARBOX_ITEMS.C */

/* What a network game on the map `map_name` (a level name, or a map's file
name) records as its version (network_game_map): for one of the game's
own multiplayer maps that this machine adds the items to (game.gearbox_items,
and the Halo PC map of its name in paths.custom_edition's maps folder),
a value of that Halo PC map's; 0 otherwise. Machines play together only
when theirs agree (cache_files_map_present). */
unsigned long gearbox_items_map_version(
	char const *map_name);

/* After an Xbox map's tags have loaded (scenario_tags_load): the items
added to them, when the map is to have them; and before its tags go
(scenario_tags_unload, after the menus' tags have), taken away. */
void gearbox_items_tags_loaded(
	char const *map_name,
	long scenario_index);
void gearbox_items_tags_unloaded(
	void);

/* whether the loaded map has the items */
boolean gearbox_items_loaded(
	void);
/* the weapon definition of one of the items' weapons (_gearbox_item_*),
or NONE when the map has none */
long gearbox_items_weapon(
	short weapon);

/* The bitmap the HUD's messaging icon `sequence_index` is drawn from, the
HUD's sheet `bitmap_group_index`: Halo PC's, for one past its end (the fuel
rod gun's: gearbox_items.c). */
long gearbox_items_messaging_icons(
	long bitmap_group_index,
	short sequence_index);

/* Each tick, after the objects' (game_tick): the items' Banshee kept inside
the map (gearbox_items.c). */
void gearbox_items_update(
	void);

/* A read (cache_file_read's) of a tag the items brought: served from the
Halo PC map's files at once, and TRUE; FALSE for any other tag's. */
boolean gearbox_items_read(
	long tag_index,
	long offset,
	long size,
	void *buffer);

#endif
