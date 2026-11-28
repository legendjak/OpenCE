/*
GEARBOX_ITEMS.C

Halo PC's multiplayer additions on the Xbox game's multiplayer maps
(gearbox_items.h): the rocket warthog, the multiplayer Banshee, the
flamethrower and the fuel rod gun, which Gearbox's port added to every
multiplayer map.

They are taken from the Halo PC map of the same name, in the maps folder of
the install paths.custom_edition names. That map is loaded beside the Xbox
map as a donor (custom_edition_cache_donor_load), converted as a Custom
Edition map is, and the tags the four items need are added to the Xbox map's
tag table, as the menus' tags are (menu_tags.c). A tag the Xbox map has too,
of the same group and name, is the Xbox map's, except the items' own (those
in their folders): Halo PC's stand in for the Xbox map's there, whose
flamethrower is an unfinished one. The donor's references are rewritten to
the tags they name in the grown table. Then the Halo PC map's placements of
the items are added to the Xbox map's scenario: its rocket warthogs and
Banshees, and the netgame equipment of its flamethrowers and fuel rod guns.

The gametype decides what of them is placed: the default vehicle set places
the Xbox map's own vehicles alone, and its PC set or custom counts place
rocket warthogs and Banshees (game_engine_vehicle_placement_allowed); the
weapon set remaps the weapons as it does the others, and a custom loadout
can start players with them (game_engine.c).

The machines of a network game must have the same tags: a game records a
value of the Halo PC map as its map's version (gearbox_items_map_version),
and a machine whose value differs cannot join it (cache_files_map_present).
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "cache/physical_memory_map.h"
#include "math/real_math.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "cache/cache_files.h"
#include "items/item_definitions.h"
#include "game/game.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "scenario/scenario_definitions.h"
#include "sound/sound_definitions.h"
#include "items/weapon_definitions.h"
#include "models/model_animation_definitions.h"
#include "text/text_group.h"
#include "interface/hud_definitions.h"
#include "bitmaps/bitmap_group.h"
#include "cache_file_formats.h"
#include "custom_edition_cache.h"
#include "mcc_cache.h"
#include "tag_schema.h"
#include "gearbox_items.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "physics/collisions.h"
#include "structures/structure_bsp_definitions.h"
#include "physics/bsp3d.h"
#include "physics/bsp2d.h"
#include "physics/collision_bsp.h"
#include "physics/collision_bsp_definitions.h"
#include "effects/decal_definitions.h"
#include "structures/structures.h"
#include "rasterizer/rasterizer_geometry.h"
#include "render/render.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xtl.h>

/* ---------- constants */

/* the import's own part of a map's version: a build that imports otherwise
records another */
#define GEARBOX_ITEMS_FORMAT 0x47425831UL
/* and with Gearbox's changes to the map (gearbox_map_change_find; 2: with
Sidewinder's arrows, whose decals' tags are added to the table; 3: and the
Banshee's flying ceilings; 4: Sidewinder's at 1; 5: its frames solid, and
its needlers Halo PC's) */
#define GEARBOX_MAP_CHANGES_FORMAT 0x4D435835UL

#define MAP_PATH_SIZE 256

#define GEARBOX_SCENARIO_TAG 'scnr'
#define GEARBOX_STRUCTURE_BSP_TAG 'sbsp'
#define GEARBOX_GLOBALS_TAG 'matg'
#define GEARBOX_VEHICLE_TAG 'vehi'
#define GEARBOX_WEAPON_TAG 'weap'
#define GEARBOX_ITEM_COLLECTION_TAG 'itmc'
#define GEARBOX_SHADER_TAG 'shdr'
#define GEARBOX_SOUND_TAG 'snd!'
#define GEARBOX_ANIMATION_GRAPH_TAG 'antr'
#define GEARBOX_STRING_LIST_TAG 'ustr'
#define GEARBOX_HUD_GLOBALS_TAG 'hudg'
#define GEARBOX_BITMAP_TAG 'bitm'
/* what a Halo PC bitmap added beside the Xbox map's of its name is called */
#define GEARBOX_OWN_NAME_SUFFIX GEARBOX_ITEMS_OWN_NAME_SUFFIX

/* a scenery placement's bytes (tag_schema_scenario.c's) */
#define SCENERY_PLACEMENT_BYTES 0x48
/* a vehicle placement's bytes (tag_schema_scenario.c's unit_placement) */
#define VEHICLE_PLACEMENT_BYTES 0x78

/* the most placements taken from the Halo PC map (its maps have at most 8
of the items' vehicles and 4 of their weapons) */
#define MAXIMUM_GEARBOX_PLACEMENTS 64

/* the most tags made for Gearbox's changes to a map (its arrows' decals),
arrows on a map, and runtime decals of a structure bsp with them */
#define MAXIMUM_GEARBOX_MADE_TAGS 4
#define MAXIMUM_GEARBOX_ARROWS 8
#define MAXIMUM_GEARBOX_RUNTIME_DECALS 1024

/* the Xbox map's blocks the animation grafts may replace, and the buffers
they may make */
#define MAXIMUM_GEARBOX_PATCHES 128
#define MAXIMUM_GEARBOX_ALLOCATIONS 512
/* a donor animation's place in a graft not yet decided */
#define GRAFT_UNSEEN (-2)
/* a seat's most weapon classes (tag_schema_models.c) */
#define GEARBOX_MAXIMUM_WEAPON_CLASSES 16

/* the first of the address set's slots */
#define REWRITTEN_INITIAL_CAPACITY 0x4000

enum
{
	_gearbox_root_rocket_warthog = 0,
	_gearbox_root_banshee,
	/* the Shade (Halo PC's gun turret: Sidewinder's, of the game's maps) */
	_gearbox_root_gun_turret,
	_gearbox_root_flamethrower,
	_gearbox_root_fuel_rod,
	NUMBER_OF_GEARBOX_ROOTS,
	/* (the weapons' roots, in _gearbox_item_* order) */
	FIRST_GEARBOX_WEAPON_ROOT = _gearbox_root_flamethrower
};

/* what became of a donor tag */
enum
{
	_donor_tag_unseen = 0,
	/* the Xbox map's of its group and name stands for it */
	_donor_tag_xbox,
	/* added to the table */
	_donor_tag_added,
	/* stands for the Xbox map's of its group and name */
	_donor_tag_replaces
};

/* ---------- structures */

/* (as cache_files.c has them) */
struct cache_file_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

struct gearbox_tag_header
{
	struct cache_file_tag_instance *instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
};

typedef char verify_gearbox_tag_instance_size[sizeof(struct cache_file_tag_instance) == 0x20 ? 1 : -1];
typedef char verify_gearbox_netgame_equipment_size[sizeof(struct scenario_netgame_equipment) == 0x90 ? 1 : -1];
typedef char verify_gearbox_palette_entry_size[sizeof(struct scenario_object_palette_entry) == 0x30 ? 1 : -1];

struct gearbox_root
{
	unsigned long group_tag;
	char const *name;
};

/* one of the Xbox map's blocks an animation graft replaced, as it was */
struct gearbox_block_patch
{
	struct tag_block *block;
	struct tag_block original;
};

/* an animation graph's graft: the donor graph's animations it takes, and
their places in the Xbox map's graph */
struct gearbox_graft
{
	struct gearbox_import const *import;
	struct animation_graph *map_graph;
	struct animation_graph *donor;
	short places[MAXIMUM_ANIMATIONS_PER_GRAPH];
	short added[MAXIMUM_ANIMATIONS_PER_GRAPH];
	short added_count;
	boolean failed;
};

/* what an import is working on */
struct gearbox_import
{
	struct cache_file_tag_instance *donor_instances;
	long donor_count;
	byte *donor_tags;
	unsigned long donor_bytes;
	struct cache_file_tag_instance *xbox_instances;
	long xbox_count;
	long first_salt;
	long added_count;
	/* for each donor tag: what became of it (_donor_tag_*) and its handle
	in the grown table */
	byte *states;
	long *handles;
	/* the names of the donor tags added beside the Xbox map's of their name
	(NULL: their own) */
	char const **own_names;
	/* the donor tags whose references are to be rewritten */
	long *queue;
	long queue_count;
	/* the references rewritten (a field is rewritten once), by address */
	unsigned long *rewritten;
	long rewritten_capacity;
	long rewritten_count;
	long roots[NUMBER_OF_GEARBOX_ROOTS];
	/* the donor's HUD messaging icons (absolute index), or NONE */
	long messaging_icons;
	/* for each of the Xbox map's tags: whether the items' tags alone use it
	(gearbox_import_items_only) */
	byte *items_only;
	/* a donor bitmap (absolute index) added beside the Xbox map's of its name
	however alike they are (Halo PC's structure bsp's lightmaps), or NONE */
	long own_index;
	/* the tags made for the map's changes (its arrows' decals), added to the
	grown table as the donor's are */
	struct cache_file_tag_instance made[MAXIMUM_GEARBOX_MADE_TAGS];
	short made_count;
	boolean failed;
};

/* what gearbox_import_items_only's walk of an Xbox map's tag marks */
struct gearbox_use_walk
{
	struct gearbox_import const *import;
	/* the tag walked (its references to itself are not uses), and whether
	it is outside the items' */
	long walked;
	boolean outside;
	/* each tag: referred to, and referred to by a tag outside the items' */
	byte *referred;
	byte *used_outside;
};

/* ---------- globals */

static struct gearbox_root const gearbox_roots[NUMBER_OF_GEARBOX_ROOTS] =
{
	{ GEARBOX_VEHICLE_TAG, "vehicles\\rwarthog\\rwarthog" },
	{ GEARBOX_VEHICLE_TAG, "vehicles\\banshee\\banshee_mp" },
	{ GEARBOX_VEHICLE_TAG, "vehicles\\c gun turret\\c gun turret_mp" },
	{ GEARBOX_WEAPON_TAG, "weapons\\flamethrower\\flamethrower" },
	{ GEARBOX_WEAPON_TAG, "weapons\\plasma_cannon\\plasma_cannon" },
};

/* the items' own folders: their tags are Halo PC's */
static char const *const gearbox_folders[] =
{
	"vehicles\\rwarthog\\",
	"vehicles\\banshee\\",
	"vehicles\\c gun turret\\",
	"weapons\\flamethrower\\",
	"weapons\\plasma_cannon\\",
};

static struct
{
	boolean loaded;
	/* the Xbox map's table, given back, and the grown one */
	struct cache_file_tag_instance *original_instances;
	long original_count;
	struct cache_file_tag_instance *instances;
	long count;
	/* for each tag of the grown table: the donor's handle it is read as, or
	NONE */
	long *donor_handles;
	/* the roots' handles in the grown table (NONE: the donor has none) */
	long roots[NUMBER_OF_GEARBOX_ROOTS];
	/* the scenario's blocks the placements were added to, as they were,
	and those that replaced them */
	struct scenario *scenario;
	struct tag_block vehicles;
	struct tag_block vehicle_palette;
	struct tag_block netgame_equipment;
	void *added_vehicles;
	void *added_vehicle_palette;
	void *added_netgame_equipment;
	/* the animation grafts' replaced blocks and buffers */
	struct gearbox_block_patch patches[MAXIMUM_GEARBOX_PATCHES];
	short patch_count;
	void *allocations[MAXIMUM_GEARBOX_ALLOCATIONS];
	short allocation_count;
	long grafted_classes;
	long extended_string_lists;
	/* Halo PC's HUD messaging icons in the grown table (NONE: none) */
	long messaging_icons;
	/* the map's flying ceiling (gearbox_map_change), if it has one */
	boolean has_flying_ceiling;
	real flying_ceiling;
	/* and Halo PC's teleporter frames to draw on it, or NULL */
	struct gearbox_map_frames const *frames;
} gearbox_items_globals;

/* the items' Banshees, each where it last was inside the map
(gearbox_items_update) */
enum
{
	GEARBOX_MAXIMUM_FLYERS = 32
};

static struct
{
	long object_index;
	real_point3d inside;
} gearbox_flyers[GEARBOX_MAXIMUM_FLYERS];
static long gearbox_next_flyer;
/* (how far below a Banshee its ground is looked for: further than any map is
tall) */
static real_vector3d const gearbox_down = { 0.0f, 0.0f, -2000.0f };

/* Halo PC's collision of the map (gearbox_barriers_load): its structure
bsp's bytes, read from the Halo PC map's file, and its collision bsp, whose
blocks are rebased onto them */
static struct
{
	boolean loaded;
	byte *data;
	struct collision_bsp bsp;
	long barrier_surfaces;
	/* (the bsp's address and size, its structure bsp, and the donor its tags
	are: the teleporters' frames are drawn from it) */
	unsigned long base;
	unsigned long size;
	struct structure_bsp const *structure_bsp;
	struct gearbox_tag_header const *donor;
} gearbox_barriers;

/* (collision_bsp.c's: a leaf, a bsp2d reference, and a surface's flag) */
struct gearbox_collision_leaf
{
	word flags;
	short bsp2d_reference_count;
	long first_bsp2d_reference_index;
};

struct gearbox_bsp2d_reference
{
	long plane_designator;
	long root_index;
};

/* (a structure bsp's header in a cache file: cache_files.c's) */
struct gearbox_structure_bsp_header
{
	unsigned long base_address;
	long vertex_buffer_count;
	unsigned long vertex_buffers;
	long index_buffer_count;
	unsigned long index_buffers;
	unsigned long signature;
};

typedef char verify_gearbox_collision_leaf_size[sizeof(struct gearbox_collision_leaf) == 8 ? 1 : -1];
typedef char verify_gearbox_bsp2d_reference_size[sizeof(struct gearbox_bsp2d_reference) == 8 ? 1 : -1];
typedef char verify_gearbox_structure_bsp_header_size[sizeof(struct gearbox_structure_bsp_header) == 0x18 ? 1 : -1];

struct gearbox_tag_header;
static void gearbox_barriers_load(char const *path, struct gearbox_tag_header const *donor);

enum
{
	GEARBOX_SURFACE_INVISIBLE_BIT = 1,
	GEARBOX_STRUCTURE_BSP_SIGNATURE = 'sbsp',
	/* (the most of a structure bsp read: Halo PC's largest is 17 MB) */
	GEARBOX_MAXIMUM_STRUCTURE_BSP_BYTES = 0x04000000
};

/* ---------- prototypes */

/* (port_config.c) */
int config_boolean(char const *name);
/* (cache_files.c) */
void *cache_files_tag_instances(long *count);
void cache_files_set_tag_instances(void *instances, long count);
boolean tag_index_is_group(long tag_index, long group_tag);

static void *gearbox_allocate(long size);
static void gearbox_patches_undo(void);

/* ---------- private code */

/* the donor of the map `name` (its file's name): its install's maps folder's
map of that name, into `path` */
static boolean gearbox_items_donor_path(
	char const *name,
	char *path)
{
	if (!custom_edition_install_present() ||
		strlen(CUSTOM_EDITION_INSTALL_MAP_DIRECTORY) + strlen(name) + 5 > MAP_PATH_SIZE)
	{
		return FALSE;
	}
	sprintf(path, "%s%s.map", CUSTOM_EDITION_INSTALL_MAP_DIRECTORY, name);

	return TRUE;
}

/* whether a tag is one of the items' own (gearbox_folders) */
static boolean gearbox_items_own(
	char const *name)
{
	short index;

	for (index = 0; index < NUMBEROF(gearbox_folders); index++)
	{
		char const *folder = gearbox_folders[index];
		char const *character = name;

		for (; *folder && *character && tolower((unsigned char)*folder) == tolower((unsigned char)*character);
			folder++, character++)
		{
		}
		if (!*folder)
			return TRUE;
	}

	return FALSE;
}

/* whether the Xbox tag can stand for the donor tag, or the donor tag for
it: of one group (or both shaders, which references name by 'shdr') */
static boolean gearbox_groups_match(
	struct cache_file_tag_instance const *map_tag,
	struct cache_file_tag_instance const *donor)
{
	return map_tag->group_tag == donor->group_tag ||
		(map_tag->parent_group_tags[0] == GEARBOX_SHADER_TAG && donor->parent_group_tags[0] == GEARBOX_SHADER_TAG);
}

/* the absolute index of the Xbox map's tag of the donor tag's group and
name, or NONE */
static long gearbox_import_xbox_tag(
	struct gearbox_import const *import,
	struct cache_file_tag_instance const *donor,
	char const *name)
{
	long index;

	for (index = 0; index < import->xbox_count; index++)
	{
		struct cache_file_tag_instance const *map_tag = &import->xbox_instances[index];

		if (map_tag->name && map_tag->base_address && gearbox_groups_match(map_tag, donor) && !csstrcasecmp(map_tag->name, name))
			return index;
	}

	return NONE;
}

/* the handle of the Xbox map's first tag of the group, or NONE */
static long gearbox_import_xbox_group(
	struct gearbox_import const *import,
	unsigned long group_tag)
{
	long index;

	for (index = 0; index < import->xbox_count; index++)
	{
		if ((unsigned long)import->xbox_instances[index].group_tag == group_tag)
			return import->xbox_instances[index].tag_index;
	}

	return NONE;
}

/* the absolute index of the donor's tag of the group and name, or NONE */
static long gearbox_import_donor_tag(
	struct gearbox_import const *import,
	unsigned long group_tag,
	char const *name)
{
	long index;

	for (index = 0; index < import->donor_count; index++)
	{
		struct cache_file_tag_instance const *donor = &import->donor_instances[index];

		if ((unsigned long)donor->group_tag == group_tag && donor->base_address &&
			!csstrcasecmp(custom_edition_cache_tag_name(import->donor_tags, import->donor_bytes, index), name))
		{
			return index;
		}
	}

	return NONE;
}

/* the absolute index of the donor tag a handle names, or NONE */
static long gearbox_import_donor_index(
	struct gearbox_import const *import,
	long handle)
{
	long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(handle);

	if (handle == NONE || index >= import->donor_count || import->donor_instances[index].tag_index != handle)
		return NONE;

	return index;
}

/* a reference of one of the Xbox map's tags (tag_validate_references_walk) */
static void gearbox_use_visit(
	void *context,
	long *tag_index,
	unsigned long group_tag,
	struct tag_reference *reference)
{
	struct gearbox_use_walk *walk = context;
	long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(*tag_index);

	(void)group_tag;
	(void)reference;
	if (*tag_index == NONE || index >= walk->import->xbox_count || index == walk->walked ||
		walk->import->xbox_instances[index].tag_index != *tag_index)
	{
		return;
	}
	walk->referred[index] = TRUE;
	if (walk->outside)
		walk->used_outside[index] = TRUE;

	return;
}

/* Which of the Xbox map's tags the items' own tags alone use: one is, when
every tag of the map that refers to it is one of the items' (gearbox_items_own),
or one of those in turn. Such a tag is only there for the items (the Xbox
map's unfinished flamethrower's HUD, sounds and animations), and Halo PC's
of its name stands for it. A tag nothing refers to (a structure bsp's, which
is not loaded yet, or one the game finds by its name) is the map's. FALSE if
it cannot be worked out. */
static boolean gearbox_import_items_only(
	struct gearbox_import *import)
{
	byte *tag_cache = physical_memory_get_tag_cache_base_address();
	struct gearbox_use_walk walk;
	boolean changed = TRUE;
	long index;

	import->items_only = malloc(import->xbox_count + 1);
	walk.import = import;
	walk.referred = malloc(import->xbox_count + 1);
	walk.used_outside = malloc(import->xbox_count + 1);
	if (!import->items_only || !walk.referred || !walk.used_outside)
	{
		if (walk.referred)
			free(walk.referred);
		if (walk.used_outside)
			free(walk.used_outside);
		return FALSE;
	}
	memset(import->items_only, FALSE, import->xbox_count);
	memset(walk.referred, FALSE, import->xbox_count);
	for (index = 0; index < import->xbox_count; index++)
	{
		struct cache_file_tag_instance const *instance = &import->xbox_instances[index];

		if (instance->name && instance->base_address && gearbox_items_own(instance->name))
			import->items_only[index] = TRUE;
	}
	/* (until no more are found: each pass marks what the others use) */
	while (changed)
	{
		changed = FALSE;
		memset(walk.used_outside, FALSE, import->xbox_count);
		for (index = 0; index < import->xbox_count; index++)
		{
			struct cache_file_tag_instance const *instance = &import->xbox_instances[index];

			if (!instance->base_address)
				continue;
			/* (the items' tags' references count as referred to, but not as
			uses outside them) */
			walk.walked = index;
			walk.outside = !import->items_only[index];
			tag_validate_references_walk((unsigned long)instance->group_tag, instance->base_address, tag_cache,
				TAG_CACHE_SIZE, gearbox_use_visit, &walk);
		}
		for (index = 0; index < import->xbox_count; index++)
		{
			if (!import->items_only[index] && walk.referred[index] && !walk.used_outside[index] &&
				import->xbox_instances[index].base_address)
			{
				import->items_only[index] = TRUE;
				changed = TRUE;
			}
		}
	}
	free(walk.referred);
	free(walk.used_outside);

	return TRUE;
}

/* Whether the Xbox map's bitmap can stand for the donor's of its name: as
many bitmaps and sequences, each bitmap of one size and format. Halo PC's
HUD bitmaps are drawn at twice the Xbox's resolution (and Halo PC's HUD
tags scaled for them as they were converted), and some have sequences the
Xbox's lack (the rocket warthog's reticle): a Halo PC tag draws Halo PC's. */
static boolean gearbox_bitmaps_alike(
	struct gearbox_import const *import,
	struct bitmap_group const *map_group,
	struct bitmap_group const *donor_group)
{
	long index;

	if (!map_group || !donor_group || map_group->bitmaps.count != donor_group->bitmaps.count ||
		map_group->sequences.count != donor_group->sequences.count)
	{
		return FALSE;
	}
	for (index = 0; index < map_group->bitmaps.count; index++)
	{
		struct bitmap_data const *map_bitmap = TAG_BLOCK_GET_ELEMENT(&map_group->bitmaps, index, struct bitmap_data);
		struct bitmap_data const *donor_bitmap = custom_edition_cache_block_element(import->donor_tags,
			import->donor_bytes, &donor_group->bitmaps, index, sizeof(*donor_bitmap));

		if (!donor_bitmap || map_bitmap->width != donor_bitmap->width || map_bitmap->height != donor_bitmap->height ||
			map_bitmap->format != donor_bitmap->format)
		{
			return FALSE;
		}
	}

	return TRUE;
}

/* The handle the donor tag (absolute index `index`) has in the grown table:
the Xbox map's of its group and name, or its own (added, or standing for the
Xbox map's), its references then to be rewritten. */
static long gearbox_import_resolve(
	struct gearbox_import *import,
	long index)
{
	struct cache_file_tag_instance const *donor = &import->donor_instances[index];
	char const *name;
	long map_tag;

	if (import->states[index] != _donor_tag_unseen)
		return import->handles[index];
	/* (the map's own scenario, bsps and globals: the donor's are the Halo PC
	map's) */
	if ((unsigned long)donor->group_tag == GEARBOX_SCENARIO_TAG ||
		(unsigned long)donor->group_tag == GEARBOX_STRUCTURE_BSP_TAG ||
		(unsigned long)donor->group_tag == GEARBOX_GLOBALS_TAG)
	{
		import->states[index] = _donor_tag_xbox;
		import->handles[index] = gearbox_import_xbox_group(import, (unsigned long)donor->group_tag);
		return import->handles[index];
	}
	name = custom_edition_cache_tag_name(import->donor_tags, import->donor_bytes, index);
	map_tag = gearbox_import_xbox_tag(import, donor, name);
	/* (a bitmap unlike the Xbox map's is added beside it, named apart, and the
	Xbox map's tags keep theirs) */
	if (map_tag != NONE && (unsigned long)donor->group_tag == GEARBOX_BITMAP_TAG && donor->base_address &&
		!gearbox_items_own(name) && !import->items_only[map_tag] &&
		(index == import->own_index ||
			!gearbox_bitmaps_alike(import, import->xbox_instances[map_tag].base_address,
				custom_edition_cache_tag_get(import->donor_tags, import->donor_bytes, (unsigned long)donor->tag_index,
					GEARBOX_BITMAP_TAG, sizeof(struct bitmap_group)))))
	{
		char *own_name = gearbox_allocate((long)(strlen(name) + strlen(GEARBOX_OWN_NAME_SUFFIX) + 1));

		if (own_name)
		{
			strcpy(own_name, name);
			strcat(own_name, GEARBOX_OWN_NAME_SUFFIX);
			import->own_names[index] = own_name;
			map_tag = NONE;
		}
	}
	if (map_tag != NONE && ((!gearbox_items_own(name) && !import->items_only[map_tag]) || !donor->base_address))
	{
		import->states[index] = _donor_tag_xbox;
		import->handles[index] = import->xbox_instances[map_tag].tag_index;
		return import->handles[index];
	}
	if (!donor->base_address)
	{
		error(_error_silent, "gearbox items: the Halo PC map's '%s' has no data", name);
		import->failed = TRUE;
		import->states[index] = _donor_tag_xbox;
		import->handles[index] = NONE;
		return NONE;
	}
	if (map_tag != NONE)
	{
		import->states[index] = _donor_tag_replaces;
		import->handles[index] = import->xbox_instances[map_tag].tag_index;
	}
	else
	{
		/* (every absolute index short of NONE's, and salts below it) */
		if (import->xbox_count + import->added_count >= UNSIGNED_SHORT_MAX ||
			import->first_salt + import->added_count >= UNSIGNED_SHORT_MAX)
		{
			error(_error_silent, "gearbox items: too many tags for the map's tag table");
			import->failed = TRUE;
			import->states[index] = _donor_tag_xbox;
			import->handles[index] = NONE;
			return NONE;
		}
		import->states[index] = _donor_tag_added;
		import->handles[index] = ((import->first_salt + import->added_count) << 16) |
			(import->xbox_count + import->added_count);
		import->added_count++;
	}
	import->queue[import->queue_count++] = index;

	return import->handles[index];
}

/* whether the field at `address` is rewritten for the first time (then
noted as rewritten) */
static boolean gearbox_import_first_rewrite(
	struct gearbox_import *import,
	void const *address)
{
	unsigned long key = (unsigned long)address;
	unsigned long slot;

	if (import->failed)
		return FALSE;
	if ((import->rewritten_count + 1) * 2 > import->rewritten_capacity)
	{
		long capacity = import->rewritten_capacity ? import->rewritten_capacity * 2 : REWRITTEN_INITIAL_CAPACITY;
		unsigned long *grown = malloc(capacity * sizeof(*grown));
		long index;

		if (!grown)
		{
			error(_error_silent, "gearbox items: out of memory for the references");
			import->failed = TRUE;
			return FALSE;
		}
		memset(grown, 0, capacity * sizeof(*grown));
		for (index = 0; index < import->rewritten_capacity; index++)
		{
			unsigned long old = import->rewritten[index];

			if (!old)
				continue;
			for (slot = ((old >> 2) * 2654435761UL) & (unsigned long)(capacity - 1); grown[slot];
				slot = (slot + 1) & (unsigned long)(capacity - 1))
			{
			}
			grown[slot] = old;
		}
		if (import->rewritten)
			free(import->rewritten);
		import->rewritten = grown;
		import->rewritten_capacity = capacity;
	}
	for (slot = ((key >> 2) * 2654435761UL) & (unsigned long)(import->rewritten_capacity - 1);
		import->rewritten[slot];
		slot = (slot + 1) & (unsigned long)(import->rewritten_capacity - 1))
	{
		if (import->rewritten[slot] == key)
			return FALSE;
	}
	import->rewritten[slot] = key;
	import->rewritten_count++;

	return TRUE;
}

/* a reference or tag index of a donor tag: the tag it names in the grown
table (tag_validate_references_walk) */
static void gearbox_import_visit(
	void *context,
	long *tag_index,
	unsigned long group_tag,
	struct tag_reference *reference)
{
	struct gearbox_import *import = context;
	long index;

	(void)group_tag;
	if (*tag_index == NONE || !gearbox_import_first_rewrite(import, tag_index))
		return;
	index = gearbox_import_donor_index(import, *tag_index);
	*tag_index = index != NONE ? gearbox_import_resolve(import, index) : NONE;
	/* (a reference is also found by its name: verify_tag_reference) */
	if (reference && index != NONE && import->own_names[index])
	{
		reference->name = (char *)import->own_names[index];
		reference->name_length = (long)strlen(import->own_names[index]);
	}

	return;
}

/* a sound's permutations name it (the sound cache reads their samples as
the tag's: cache_tag_index, and runtime_tag_index) */
static void gearbox_import_sound(
	struct gearbox_import *import,
	struct sound_definition *sound,
	long handle)
{
	long range_index;

	for (range_index = 0; range_index < sound->pitch_ranges.count; range_index++)
	{
		struct sound_pitch_range *range = custom_edition_cache_block_element(import->donor_tags,
			import->donor_bytes, &sound->pitch_ranges, range_index, sizeof(*range));
		long permutation_index;

		for (permutation_index = 0; range && permutation_index < range->permutations.count; permutation_index++)
		{
			struct sound_permutation *permutation = custom_edition_cache_block_element(import->donor_tags,
				import->donor_bytes, &range->permutations, permutation_index, sizeof(*permutation));

			if (permutation)
			{
				permutation->unknown2 = (unsigned long)handle;
				permutation->unknown3 = (unsigned long)handle;
			}
		}
	}

	return;
}

/* the references of every donor tag queued rewritten, and of those they
queue in turn */
static void gearbox_import_rewrite(
	struct gearbox_import *import)
{
	long queued;

	for (queued = 0; queued < import->queue_count && !import->failed; queued++)
	{
		long index = import->queue[queued];
		struct cache_file_tag_instance *donor = &import->donor_instances[index];

		if (!tag_validate_references_walk((unsigned long)donor->group_tag, donor->base_address, import->donor_tags,
			import->donor_bytes, gearbox_import_visit, import))
		{
			error(_error_silent, "gearbox items: the references of the Halo PC map's '%s' cannot be followed",
				custom_edition_cache_tag_name(import->donor_tags, import->donor_bytes, index));
			import->failed = TRUE;
			break;
		}
		if ((unsigned long)donor->group_tag == GEARBOX_SOUND_TAG)
			gearbox_import_sound(import, donor->base_address, import->handles[index]);
	}

	return;
}

/* whether the donor's item collection (absolute index) holds one of the
items' weapons */
static boolean gearbox_import_collection_has_root(
	struct gearbox_import const *import,
	long collection_index)
{
	struct item_collection_definition *collection;
	long permutation_index;

	if (collection_index == NONE)
		return FALSE;
	collection = custom_edition_cache_tag_get(import->donor_tags, import->donor_bytes,
		(unsigned long)import->donor_instances[collection_index].tag_index, GEARBOX_ITEM_COLLECTION_TAG,
		sizeof(*collection));
	for (permutation_index = 0; collection && permutation_index < collection->permutations.count; permutation_index++)
	{
		struct item_permutation_definition *permutation = custom_edition_cache_block_element(import->donor_tags,
			import->donor_bytes, &collection->permutations, permutation_index, sizeof(*permutation));
		long item;
		short root;

		if (!permutation)
			break;
		item = gearbox_import_donor_index(import, permutation->item.index);
		for (root = FIRST_GEARBOX_WEAPON_ROOT; item != NONE && root < NUMBER_OF_GEARBOX_ROOTS; root++)
		{
			if (item == import->roots[root])
				return TRUE;
		}
	}

	return FALSE;
}

/* the root vehicle a donor vehicle placement places, or NONE */
static short gearbox_import_vehicle_root(
	struct gearbox_import const *import,
	struct scenario *donor_scenario,
	byte const *placement)
{
	struct scenario_object_datum const *object = (struct scenario_object_datum const *)placement;
	struct scenario_object_palette_entry *entry;
	long vehicle;
	short root;

	if (object->palette_entry_index < 0)
		return NONE;
	entry = custom_edition_cache_block_element(import->donor_tags, import->donor_bytes,
		&donor_scenario->vehicle_palette, object->palette_entry_index, sizeof(*entry));
	vehicle = entry ? gearbox_import_donor_index(import, entry->reference.index) : NONE;
	for (root = 0; vehicle != NONE && root < FIRST_GEARBOX_WEAPON_ROOT; root++)
	{
		if (vehicle == import->roots[root])
			return root;
	}

	return NONE;
}

/* a block grown by `added` elements of `element_bytes` taken from the
donor's block: the new elements, or NULL */
static byte *gearbox_block_grow(
	struct tag_block const *block,
	long element_bytes,
	long added)
{
	byte *elements = malloc((block->count + added) * element_bytes + 1);

	if (elements && block->count > 0)
		memcpy(elements, block->address, block->count * element_bytes);

	return elements;
}

/* The Halo PC map's placements of the items added to the Xbox map's
scenario: its rocket warthogs and Banshees, and the netgame equipment of
its flamethrowers and fuel rod guns. FALSE if they cannot be. */
static boolean gearbox_import_placements(
	struct gearbox_import *import,
	struct scenario *donor_scenario,
	struct scenario *scenario,
	long const *vehicle_placements,
	short const *vehicle_roots,
	long vehicle_count,
	long const *equipment,
	long equipment_count)
{
	short palette_indices[FIRST_GEARBOX_WEAPON_ROOT];
	long palette_added = 0;
	byte *palette = NULL;
	byte *vehicles = NULL;
	byte *netgame_equipment = NULL;
	long index;
	short root;

	for (root = 0; root < FIRST_GEARBOX_WEAPON_ROOT; root++)
	{
		palette_indices[root] = NONE;
		for (index = 0; index < vehicle_count; index++)
		{
			if (vehicle_roots[index] == root)
			{
				palette_indices[root] = (short)(scenario->vehicle_palette.count + palette_added++);
				break;
			}
		}
	}
	if (vehicle_count)
	{
		palette = gearbox_block_grow(&scenario->vehicle_palette, sizeof(struct scenario_object_palette_entry),
			palette_added);
		vehicles = gearbox_block_grow(&scenario->vehicles, VEHICLE_PLACEMENT_BYTES, vehicle_count);
	}
	if (equipment_count)
	{
		netgame_equipment = gearbox_block_grow(&scenario->netgame_equipment,
			sizeof(struct scenario_netgame_equipment), equipment_count);
	}
	if ((vehicle_count && (!palette || !vehicles)) || (equipment_count && !netgame_equipment))
	{
		error(_error_silent, "gearbox items: out of memory for the placements");
		if (palette)
			free(palette);
		if (vehicles)
			free(vehicles);
		if (netgame_equipment)
			free(netgame_equipment);
		return FALSE;
	}

	/* the vehicles: a palette entry for each of the items' vehicles placed,
	naming its tag in the grown table */
	for (root = 0; root < FIRST_GEARBOX_WEAPON_ROOT; root++)
	{
		struct scenario_object_palette_entry *entry;

		if (palette_indices[root] == NONE)
			continue;
		entry = (struct scenario_object_palette_entry *)palette + palette_indices[root];
		memset(entry, 0, sizeof(*entry));
		entry->reference.group_tag = GEARBOX_VEHICLE_TAG;
		entry->reference.name = (char *)gearbox_roots[root].name;
		entry->reference.name_length = (long)strlen(gearbox_roots[root].name);
		entry->reference.index = gearbox_import_resolve(import, import->roots[root]);
	}
	for (index = 0; index < vehicle_count; index++)
	{
		byte const *source = custom_edition_cache_block_element(import->donor_tags, import->donor_bytes,
			&donor_scenario->vehicles, vehicle_placements[index], VEHICLE_PLACEMENT_BYTES);
		byte *placement = vehicles + (scenario->vehicles.count + index) * VEHICLE_PLACEMENT_BYTES;
		struct scenario_object_datum *object = (struct scenario_object_datum *)placement;

		memcpy(placement, source, VEHICLE_PLACEMENT_BYTES);
		object->palette_entry_index = palette_indices[vehicle_roots[index]];
		/* (the Halo PC map's object names are not the Xbox map's) */
		object->name_index = NONE;
	}

	/* the netgame equipment, its item collections in the grown table */
	for (index = 0; index < equipment_count; index++)
	{
		struct scenario_netgame_equipment const *source = custom_edition_cache_block_element(import->donor_tags,
			import->donor_bytes, &donor_scenario->netgame_equipment, equipment[index], sizeof(*source));
		struct scenario_netgame_equipment *added =
			(struct scenario_netgame_equipment *)netgame_equipment + scenario->netgame_equipment.count + index;
		long collection = gearbox_import_donor_index(import, source->item_collection.index);

		*added = *source;
		added->run_time_spawned_item_index = NONE;
		added->item_collection.index = collection != NONE ? gearbox_import_resolve(import, collection) : NONE;
	}

	gearbox_items_globals.scenario = scenario;
	gearbox_items_globals.vehicles = scenario->vehicles;
	gearbox_items_globals.vehicle_palette = scenario->vehicle_palette;
	gearbox_items_globals.netgame_equipment = scenario->netgame_equipment;
	if (vehicle_count)
	{
		gearbox_items_globals.added_vehicle_palette = palette;
		gearbox_items_globals.added_vehicles = vehicles;
		scenario->vehicle_palette.address = palette;
		scenario->vehicle_palette.count += palette_added;
		scenario->vehicles.address = vehicles;
		scenario->vehicles.count += vehicle_count;
	}
	if (equipment_count)
	{
		gearbox_items_globals.added_netgame_equipment = netgame_equipment;
		scenario->netgame_equipment.address = netgame_equipment;
		scenario->netgame_equipment.count += equipment_count;
	}

	return TRUE;
}

/* the grown table: the Xbox map's tags, those that stand for some of them,
and those added; FALSE if it cannot be made */
static boolean gearbox_import_table(
	struct gearbox_import *import)
{
	long count = import->xbox_count + import->added_count;
	struct cache_file_tag_instance *instances = malloc(count * sizeof(*instances));
	long *donor_handles = malloc(count * sizeof(*donor_handles));
	long index;

	if (!instances || !donor_handles)
	{
		error(_error_silent, "gearbox items: out of memory for the tag table");
		if (instances)
			free(instances);
		if (donor_handles)
			free(donor_handles);
		return FALSE;
	}
	memcpy(instances, import->xbox_instances, import->xbox_count * sizeof(*instances));
	for (index = 0; index < count; index++)
		donor_handles[index] = NONE;
	for (index = 0; index < import->donor_count; index++)
	{
		struct cache_file_tag_instance const *donor = &import->donor_instances[index];
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(import->handles[index]);
		struct cache_file_tag_instance *instance = &instances[absolute_index];

		if (import->states[index] == _donor_tag_added)
		{
			*instance = *donor;
			instance->name = import->own_names[index] ? (char *)import->own_names[index] :
				(char *)custom_edition_cache_tag_name(import->donor_tags, import->donor_bytes, index);
			instance->tag_index = import->handles[index];
		}
		else if (import->states[index] == _donor_tag_replaces)
		{
			/* (its handle and name stay the Xbox map's) */
			instance->group_tag = donor->group_tag;
			instance->parent_group_tags[0] = donor->parent_group_tags[0];
			instance->parent_group_tags[1] = donor->parent_group_tags[1];
			instance->base_address = donor->base_address;
		}
		else
		{
			continue;
		}
		donor_handles[absolute_index] = donor->tag_index;
	}
	for (index = 0; index < import->made_count; index++)
		instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(import->made[index].tag_index)] = import->made[index];
	gearbox_items_globals.original_instances = import->xbox_instances;
	gearbox_items_globals.original_count = import->xbox_count;
	gearbox_items_globals.instances = instances;
	gearbox_items_globals.count = count;
	gearbox_items_globals.donor_handles = donor_handles;
	cache_files_set_tag_instances(instances, count);

	return TRUE;
}

static void gearbox_import_dispose(
	struct gearbox_import *import)
{
	if (import->states)
		free(import->states);
	if (import->handles)
		free(import->handles);
	if (import->queue)
		free(import->queue);
	if (import->rewritten)
		free(import->rewritten);
	if (import->items_only)
		free(import->items_only);
	if (import->own_names)
		free((void *)import->own_names);

	return;
}

/* ---- the items' weapons held: a unit holds a weapon its animation graph
has a weapon class for, by the weapon's label (units.c,
unit_set_or_test_seat_and_weapon_label). The Xbox game's players could not
hold the fuel rod gun (the Covenant's alone), so the Xbox map's player
graph has no class for it; Halo PC's graph of the same name does. Its
classes for the items' weapons that the Xbox map's graph has none for are
grafted onto that graph's seats, with the animations they play; the Xbox
graph's own classes and animations stay as they are. */

/* a zeroed buffer kept until the map's tags go */
static void *gearbox_allocate(
	long size)
{
	void *block;

	if (gearbox_items_globals.allocation_count >= MAXIMUM_GEARBOX_ALLOCATIONS)
		return NULL;
	block = malloc(size > 0 ? size : 1);
	if (block)
	{
		memset(block, 0, size > 0 ? size : 1);
		gearbox_items_globals.allocations[gearbox_items_globals.allocation_count++] = block;
	}

	return block;
}

/* the block given elements of its own, the one it had kept to give back */
static boolean gearbox_block_replace(
	struct tag_block *block,
	long count,
	void *elements)
{
	struct gearbox_block_patch *patch;

	if (gearbox_items_globals.patch_count >= MAXIMUM_GEARBOX_PATCHES)
		return FALSE;
	patch = &gearbox_items_globals.patches[gearbox_items_globals.patch_count++];
	patch->block = block;
	patch->original = *block;
	block->count = count;
	block->address = elements;

	return TRUE;
}

/* the blocks given back, newest first, and the buffers let go */
static void gearbox_patches_undo(
	void)
{
	while (gearbox_items_globals.patch_count > 0)
	{
		struct gearbox_block_patch *patch = &gearbox_items_globals.patches[--gearbox_items_globals.patch_count];

		*patch->block = patch->original;
	}
	while (gearbox_items_globals.allocation_count > 0)
		free(gearbox_items_globals.allocations[--gearbox_items_globals.allocation_count]);

	return;
}

static void *gearbox_donor_element(
	struct gearbox_import const *import,
	struct tag_block const *block,
	long index,
	unsigned long element_bytes)
{
	return custom_edition_cache_block_element(import->donor_tags, import->donor_bytes, block, index, element_bytes);
}

/* whether one of the Xbox map's seats has a weapon class for the label */
static boolean gearbox_xbox_seat_holds(
	struct animation_graph_unit_seat const *seat,
	char const *label)
{
	long class_index;

	for (class_index = 0; class_index < seat->weapon_classes.count; class_index++)
	{
		struct animation_graph_weapon_class const *weapon_class =
			TAG_BLOCK_GET_ELEMENT(&seat->weapon_classes, class_index, struct animation_graph_weapon_class);
		long type_index;

		for (type_index = 0; type_index < weapon_class->weapon_types.count; type_index++)
		{
			if (!csstrcasecmp(TAG_BLOCK_GET_ELEMENT(&weapon_class->weapon_types, type_index,
				struct animation_graph_weapon_type)->label, label))
			{
				return TRUE;
			}
		}
	}

	return FALSE;
}

/* the donor seat's weapon class for the label, or NULL */
static struct animation_graph_weapon_class *gearbox_donor_seat_class(
	struct gearbox_import const *import,
	struct animation_graph_unit_seat const *seat,
	char const *label)
{
	long class_index;

	for (class_index = 0; class_index < seat->weapon_classes.count; class_index++)
	{
		struct animation_graph_weapon_class *weapon_class =
			gearbox_donor_element(import, &seat->weapon_classes, class_index, sizeof(*weapon_class));
		long type_index;

		for (type_index = 0; weapon_class && type_index < weapon_class->weapon_types.count; type_index++)
		{
			struct animation_graph_weapon_type const *type =
				gearbox_donor_element(import, &weapon_class->weapon_types, type_index, sizeof(*type));

			if (type && !csstrcasecmp(type->label, label))
				return weapon_class;
		}
	}

	return NULL;
}

/* the place in the Xbox map's graph of the donor graph's animation: the
Xbox graph's of its name, else one added after its own (and what it leads
to with it) */
static short gearbox_graft_place(
	struct gearbox_graft *graft,
	short donor_index)
{
	struct animation const *animation;
	long index;

	if (donor_index == NONE)
		return NONE;
	if (donor_index < 0 || donor_index >= graft->donor->animations.count ||
		donor_index >= MAXIMUM_ANIMATIONS_PER_GRAPH)
	{
		graft->failed = TRUE;
		return NONE;
	}
	if (graft->places[donor_index] != GRAFT_UNSEEN)
		return graft->places[donor_index];
	animation = gearbox_donor_element(graft->import, &graft->donor->animations, donor_index, sizeof(*animation));
	if (!animation)
	{
		graft->failed = TRUE;
		return NONE;
	}
	for (index = 0; index < graft->map_graph->animations.count; index++)
	{
		if (!csstrcasecmp(TAG_BLOCK_GET_ELEMENT(&graft->map_graph->animations, index, struct animation)->name,
			animation->name))
		{
			graft->places[donor_index] = (short)index;
			return (short)index;
		}
	}
	if (graft->map_graph->animations.count + graft->added_count >= MAXIMUM_ANIMATIONS_PER_GRAPH)
	{
		graft->failed = TRUE;
		return NONE;
	}
	graft->places[donor_index] = (short)(graft->map_graph->animations.count + graft->added_count);
	graft->added[graft->added_count++] = donor_index;
	gearbox_graft_place(graft, animation->next_animation_index);
	gearbox_graft_place(graft, animation->runtime_parent_animation_index);

	return graft->places[donor_index];
}

/* the donor's list of animation indices `source` (a block in the donor's
tags) in the Xbox map's graph's places, into `block` (its copy's, given a
buffer of its own) */
static boolean gearbox_graft_index_list(
	struct gearbox_graft *graft,
	struct tag_block const *source_block,
	struct tag_block *block)
{
	struct animation_graph_animation_index *indices;
	long index;

	if (source_block->count <= 0)
		return TRUE;
	indices = gearbox_allocate(source_block->count * sizeof(*indices));
	if (!indices)
		return FALSE;
	for (index = 0; index < source_block->count; index++)
	{
		struct animation_graph_animation_index const *source =
			gearbox_donor_element(graft->import, source_block, index, sizeof(*source));

		if (!source)
		{
			return FALSE;
		}
		indices[index].animation_index = gearbox_graft_place(graft, source->animation_index);
	}
	block->address = indices;

	return !graft->failed;
}

/* a donor weapon class, copied with its lists in the Xbox map's graph's
places, into `copy` */
static boolean gearbox_graft_class(
	struct gearbox_graft *graft,
	struct animation_graph_weapon_class const *weapon_class,
	struct animation_graph_weapon_class *copy)
{
	struct animation_graph_weapon_type *types = NULL;
	long index;

	*copy = *weapon_class;
	if (!gearbox_graft_index_list(graft, &weapon_class->animations, &copy->animations))
		return FALSE;
	if (copy->weapon_types.count > 0)
	{
		types = gearbox_allocate(copy->weapon_types.count * sizeof(*types));
		if (!types)
			return FALSE;
		for (index = 0; index < copy->weapon_types.count; index++)
		{
			struct animation_graph_weapon_type const *type =
				gearbox_donor_element(graft->import, &weapon_class->weapon_types, index, sizeof(*type));

			if (!type)
			{
				return FALSE;
			}
			types[index] = *type;
			if (!gearbox_graft_index_list(graft, &type->animations, &types[index].animations))
				return FALSE;
		}
		copy->weapon_types.address = types;
	}
	/* (its ik points are names, read where they are) */
	if (copy->ik_points.count > 0 && !gearbox_donor_element(graft->import, &weapon_class->ik_points,
		copy->ik_points.count - 1, sizeof(struct animation_graph_ik_point)))
	{
		return FALSE;
	}

	return TRUE;
}

/* The items' weapons' classes the Xbox map's graph `map_graph` has none for,
taken from the donor's graph of its name onto its seats. FALSE only when
the map's tags cannot be given back as they were. */
static boolean gearbox_graft_graph(
	struct gearbox_import const *import,
	struct animation_graph *map_graph,
	struct animation_graph *donor,
	char const *const *labels,
	short label_count)
{
	struct gearbox_graft *graft = malloc(sizeof(struct gearbox_graft));
	struct animation_graph_weapon_class *classes[GEARBOX_MAXIMUM_WEAPON_CLASSES];
	struct animation *animations;
	long seat_index;
	long grafted = 0;
	short index;

	if (!graft)
		return TRUE;
	graft->import = import;
	graft->map_graph = map_graph;
	graft->donor = donor;
	graft->added_count = 0;
	graft->failed = FALSE;
	for (index = 0; index < MAXIMUM_ANIMATIONS_PER_GRAPH; index++)
		graft->places[index] = GRAFT_UNSEEN;
	for (seat_index = 0; seat_index < map_graph->unit_seats.count && !graft->failed; seat_index++)
	{
		struct animation_graph_unit_seat *seat =
			TAG_BLOCK_GET_ELEMENT(&map_graph->unit_seats, seat_index, struct animation_graph_unit_seat);
		struct animation_graph_unit_seat const *donor_seat = NULL;
		struct animation_graph_weapon_class *seat_classes;
		short class_count = 0;
		long donor_seat_index;

		for (donor_seat_index = 0; donor_seat_index < donor->unit_seats.count && !donor_seat; donor_seat_index++)
		{
			struct animation_graph_unit_seat const *candidate =
				gearbox_donor_element(import, &donor->unit_seats, donor_seat_index, sizeof(*candidate));

			if (candidate && !csstrcasecmp(candidate->label, seat->label))
				donor_seat = candidate;
		}
		for (index = 0; donor_seat && index < label_count; index++)
		{
			struct animation_graph_weapon_class *weapon_class;
			short known;

			if (gearbox_xbox_seat_holds(seat, labels[index]))
				continue;
			weapon_class = gearbox_donor_seat_class(import, donor_seat, labels[index]);
			for (known = 0; weapon_class && known < class_count; known++)
			{
				if (classes[known] == weapon_class)
					weapon_class = NULL;
			}
			if (weapon_class && seat->weapon_classes.count + class_count < GEARBOX_MAXIMUM_WEAPON_CLASSES)
				classes[class_count++] = weapon_class;
		}
		if (!class_count)
			continue;
		seat_classes = gearbox_allocate((seat->weapon_classes.count + class_count) * sizeof(*seat_classes));
		if (!seat_classes)
		{
			graft->failed = TRUE;
			break;
		}
		if (seat->weapon_classes.count > 0)
			memcpy(seat_classes, seat->weapon_classes.address, seat->weapon_classes.count * sizeof(*seat_classes));
		for (index = 0; index < class_count && !graft->failed; index++)
		{
			if (!gearbox_graft_class(graft, classes[index], &seat_classes[seat->weapon_classes.count + index]))
				graft->failed = TRUE;
		}
		if (graft->failed || !gearbox_block_replace(&seat->weapon_classes, seat->weapon_classes.count + class_count,
			seat_classes))
		{
			graft->failed = TRUE;
			break;
		}
		grafted += class_count;
	}
	/* the animations they play that the graph has none of, after its own */
	if (!graft->failed && graft->added_count)
	{
		animations = gearbox_allocate((map_graph->animations.count + graft->added_count) * sizeof(*animations));
		if (!animations)
		{
			graft->failed = TRUE;
		}
		else
		{
			memcpy(animations, map_graph->animations.address, map_graph->animations.count * sizeof(*animations));
			for (index = 0; index < graft->added_count; index++)
			{
				struct animation *animation = &animations[map_graph->animations.count + index];

				*animation = *(struct animation *)gearbox_donor_element(import, &donor->animations,
					graft->added[index], sizeof(*animation));
				animation->next_animation_index = animation->next_animation_index == NONE ? NONE :
					graft->places[animation->next_animation_index];
				animation->runtime_parent_animation_index = animation->runtime_parent_animation_index == NONE ? NONE :
					graft->places[animation->runtime_parent_animation_index];
				/* (its sounds are the donor graph's list's) */
				animation->sound_index = NONE;
			}
			if (!gearbox_block_replace(&map_graph->animations, map_graph->animations.count + graft->added_count, animations))
				graft->failed = TRUE;
		}
	}
	if (graft->failed)
	{
		free(graft);
		return FALSE;
	}
	gearbox_items_globals.grafted_classes += grafted;
	free(graft);

	return TRUE;
}

/* every Xbox map's animation graph whose Halo PC graph of its name has
classes for the items' weapons that it has none for; FALSE if one cannot be
grafted (none is then) */
static boolean gearbox_import_grafts(
	struct gearbox_import const *import)
{
	char const *labels[NUMBER_OF_GEARBOX_WEAPONS];
	short label_count = 0;
	short root;
	long index;

	for (root = FIRST_GEARBOX_WEAPON_ROOT; root < NUMBER_OF_GEARBOX_ROOTS; root++)
	{
		struct weapon_definition *weapon = import->roots[root] == NONE ? NULL :
			custom_edition_cache_tag_get(import->donor_tags, import->donor_bytes,
				(unsigned long)import->donor_instances[import->roots[root]].tag_index, GEARBOX_WEAPON_TAG,
				sizeof(*weapon));

		if (weapon && weapon->weapon.label[0] && memchr(weapon->weapon.label, 0, sizeof(weapon->weapon.label)))
			labels[label_count++] = weapon->weapon.label;
	}
	for (index = 0; label_count && index < import->xbox_count; index++)
	{
		struct cache_file_tag_instance const *instance = &import->xbox_instances[index];
		struct animation_graph *donor;
		long donor_index;

		if ((unsigned long)instance->group_tag != GEARBOX_ANIMATION_GRAPH_TAG || !instance->base_address ||
			!instance->name)
		{
			continue;
		}
		donor_index = gearbox_import_donor_tag(import, GEARBOX_ANIMATION_GRAPH_TAG, instance->name);
		donor = donor_index == NONE ? NULL : custom_edition_cache_tag_get(import->donor_tags, import->donor_bytes,
			(unsigned long)import->donor_instances[donor_index].tag_index, GEARBOX_ANIMATION_GRAPH_TAG, sizeof(*donor));
		if (donor && !gearbox_graft_graph(import, instance->base_address, donor, labels, label_count))
		{
			error(_error_silent, "gearbox items: Halo PC's animations for the items' weapons cannot be added to '%s'",
				instance->name);
			gearbox_patches_undo();
			return FALSE;
		}
	}

	return TRUE;
}

/* ---- the items' text: Halo PC added the fuel rod gun's to the end of the
HUD's item messages ("Picked up a fuel rod gun"), which an item names by
its index, past the Xbox map's list's end. Halo PC's list stands for the
Xbox map's when it is the Xbox map's, string for string, with more after
them: every index the Xbox map's tags name finds the same string. (Only
that list: Halo PC's longer multiplayer and pause menu lists would change
what the Xbox map's game says and shows.) Its messaging icons (the one
"Hold X to swap for" shows) are drawn otherwise: Halo PC's sheet is of
another format and its sprites a little smaller, so the Xbox map's stays,
and an icon past its end is drawn from Halo PC's
(gearbox_items_messaging_icons). */

/* whether the donor string `donor_string` (an element of a list in the
donor's tags) is the Xbox map's `map_string` */
static boolean gearbox_strings_match(
	struct gearbox_import const *import,
	struct tag_data const *map_string,
	struct tag_data const *donor_string)
{
	uint32_t donor_size;
	void *donor_text;

	if (!donor_string)
		return FALSE;
	if (map_string && map_string->size == 0 && donor_string->size == 0)
		return TRUE;
	donor_text = custom_edition_cache_data_get(import->donor_tags, import->donor_bytes, donor_string, &donor_size);
	if (!donor_text)
		return FALSE;
	/* (one past the Xbox list's end only lies in the donor's tags) */
	return !map_string || (map_string->size == (long)donor_size && map_string->address &&
		!memcmp(map_string->address, donor_text, donor_size));
}

/* whether the Xbox map's tag (absolute index) is the item messages list of
one of its HUD globals */
static boolean gearbox_item_messages(
	struct gearbox_import const *import,
	long tag_index)
{
	long index;

	for (index = 0; index < import->xbox_count; index++)
	{
		struct cache_file_tag_instance const *instance = &import->xbox_instances[index];
		struct hud_globals_definition const *hud_globals = instance->base_address;

		if ((unsigned long)instance->group_tag == GEARBOX_HUD_GLOBALS_TAG && hud_globals &&
			hud_globals->messaging.hud_item_messages.index != NONE &&
			DATUM_INDEX_TO_ABSOLUTE_INDEX(hud_globals->messaging.hud_item_messages.index) == tag_index)
		{
			return TRUE;
		}
	}

	return FALSE;
}

/* the Xbox map's HUD item messages Halo PC's of their name extend, Halo
PC's in the grown table; how many lists */
static long gearbox_import_string_lists(
	struct gearbox_import const *import)
{
	long replaced = 0;
	long index;

	for (index = 0; index < import->xbox_count; index++)
	{
		struct cache_file_tag_instance *instance = &gearbox_items_globals.instances[index];
		struct string_list const *map_list = instance->base_address;
		struct string_list const *donor_list;
		long donor_index, string_index;
		boolean extends = TRUE;

		if ((unsigned long)instance->group_tag != GEARBOX_STRING_LIST_TAG || !map_list || !instance->name ||
			gearbox_items_globals.donor_handles[index] != NONE || !gearbox_item_messages(import, index))
		{
			continue;
		}
		donor_index = gearbox_import_donor_tag(import, GEARBOX_STRING_LIST_TAG, instance->name);
		donor_list = donor_index == NONE ? NULL : custom_edition_cache_tag_get(import->donor_tags, import->donor_bytes,
			(unsigned long)import->donor_instances[donor_index].tag_index, GEARBOX_STRING_LIST_TAG, sizeof(*donor_list));
		if (!donor_list || donor_list->strings.count <= map_list->strings.count)
			continue;
		for (string_index = 0; extends && string_index < donor_list->strings.count; string_index++)
		{
			extends = gearbox_strings_match(import,
				string_index < map_list->strings.count ?
					TAG_BLOCK_GET_ELEMENT(&map_list->strings, string_index, struct tag_data) : NULL,
				gearbox_donor_element(import, &donor_list->strings, string_index, sizeof(struct tag_data)));
		}
		if (!extends)
			continue;
		instance->base_address = import->donor_instances[donor_index].base_address;
		gearbox_items_globals.donor_handles[index] = import->donor_instances[donor_index].tag_index;
		replaced++;
	}

	return replaced;
}

/* ---- Gearbox's changes to the Xbox's maps (game.gearbox_map_changes) */

/* an arrow Gearbox painted on a map's floor, in Halo PC's structure bsp:
drawn on the Xbox map's as a decal, a copy of one of the Xbox map's with the
Halo PC map's bitmap of the arrow (its tip to the bitmap's top) */
struct gearbox_map_arrow
{
	char const *bitmap;
	/* the decal's name */
	char const *decal;
	/* the arrow's middle on the floor, the way its tip points, and half its
	size */
	real_point3d position;
	real_vector2d tip;
	real radius;
};

/* a map's arrows, and the Xbox map's decal theirs are copies of */
struct gearbox_map_arrows
{
	char const *template_decal;
	struct gearbox_map_arrow const *arrows;
	short count;
};

/* a frame Gearbox built around a teleporter: the teleporter's place (on
its floor) and the way its field faces (into the wall), and the Xbox map's
own frame of the same shape that it is lit as: its teleporter's place and
its field's way */
struct gearbox_map_frame
{
	real_point3d position;
	real yaw;
	real_point3d lit_position;
	real lit_yaw;
};

/* a solid part of a frame: a box in its own axes, from its teleporter on
its floor: across its field, into the wall, and up */
struct gearbox_frame_box
{
	real across[2];
	real depth[2];
	real height[2];
};

/* the frames Gearbox built around teleporters in Halo PC's structure bsp
that the Xbox map's has not: its triangles of their shaders near the
teleporters (gearbox_frames_build), and their solid parts */
struct gearbox_map_frames
{
	char const *const *shaders;
	short shader_count;
	/* (across, from a teleporter to a triangle's middle) */
	real radius;
	struct gearbox_map_frame const *frames;
	short count;
	struct gearbox_frame_box const *boxes;
	short box_count;
	/* the Xbox frame's doorway's back (its surfaces within `doorway` across
	and behind its front), drawn this far into the wall: in front of the Xbox
	map's wall where the frame stands, behind which it is */
	real doorway;
	real back_depth;
};

/* the scenery and netgame equipment placements Gearbox changed on a map: of
the scenery (by its tag) and item collections listed, the Halo PC map's
placements stand for the Xbox map's (none, where it has none), and with
teleporters, its teleporters' netgame flags for the Xbox map's */
struct gearbox_map_change
{
	/* the map (its file's name) */
	char const *map_name;
	char const *const *scenery;
	char const *const *equipment;
	boolean teleporters;
	/* Halo PC's structure bsp (gearbox_map_bsp_read), and with it, the Xbox
	map's scenery of these tags taken away and its decals: what Halo PC's
	geometry has in their place */
	boolean structure_bsp;
	char const *const *bsp_scenery;
	boolean bsp_no_decals;
	/* with the Xbox map's structure bsp, Halo PC's arrows on it, or NULL */
	struct gearbox_map_arrows const *arrows;
	/* and Halo PC's teleporter frames (gearbox_frames_build), or NULL */
	struct gearbox_map_frames const *frames;
	/* with a flying ceiling, the highest a Banshee flies (its origin): its
	camera is then below the lowest of the walls around the map, which hide
	all outside it */
	boolean has_flying_ceiling;
	real flying_ceiling;
};

/* the Halo PC map's placements an import takes for a map's changes (indices
in its scenario's blocks) */
struct gearbox_change_selection
{
	struct gearbox_map_change const *change;
	long scenery[MAXIMUM_GEARBOX_PLACEMENTS];
	long scenery_count;
	long equipment[MAXIMUM_GEARBOX_PLACEMENTS];
	long equipment_count;
	long flags[MAXIMUM_GEARBOX_PLACEMENTS];
	long flag_count;
};

/* Blood Gulch: its large shrub off the ground set on it, and another
beside it. Sidewinder: a teleporter in each base's room, where the Xbox's
has a teleporter's pad, red and blue beacons at the bases' doors and the
teleporters, and two of each power-up in the middle tunnel, where the
Xbox's has one. */
static char const *const gearbox_bloodgulch_scenery[] =
{
	"scenery\\shrubs\\shrub_large\\shrub_large",
	NULL
};
static char const *const gearbox_sidewinder_scenery[] =
{
	"scenery\\teleporter_shield\\teleporter",
	"scenery\\teleporter_base\\teleporter_base",
	"levels\\test\\timberland\\scenery\\mp_beacon_blue\\mp_beacon_blue",
	"levels\\test\\timberland\\scenery\\mp_beacon_red\\mp_beacon_red",
	NULL
};
static char const *const gearbox_sidewinder_equipment[] =
{
	"item collections\\powerups\\powerup super shield",
	"item collections\\powerups\\powerup invisibility",
	/* (the needlers beside the teleporters Gearbox moved, which would stand
	in their frames' columns, at Gearbox's places in the rooms) */
	"item collections\\single weapons\\needler",
	NULL
};

/* Sidewinder's middle tunnel: the Xbox's thin pillar stood in a blast door's
collision, which Halo PC's thick pillar has in its geometry */
static char const *const gearbox_sidewinder_bsp_scenery[] =
{
	"levels\\a10\\devices\\doors\\door_blast_collision\\door_blast_collision",
	NULL
};

/* Sidewinder's arrows to its teleporters: a red and a blue one to those to
the bases, and a white one to each of the two between its sides (Halo PC's
`dangercanyon_arrow` shaders' triangles, square: Halo PC's are 15% longer
than wide) */
static struct gearbox_map_arrow const gearbox_sidewinder_arrow_list[] =
{
	{ "levels\\test\\dangercanyon\\bitmaps\\dangercanyon_arrow_red",
		"levels\\test\\sidewinder\\decals-environment\\arrow_red", { -51.42f, 24.45f, 0.13f }, { -0.42f, -0.91f }, 0.63f },
	{ "levels\\test\\dangercanyon\\bitmaps\\dangercanyon_arrow_white",
		"levels\\test\\sidewinder\\decals-environment\\arrow_white", { -42.15f, 44.34f, 0.13f }, { 0.42f, 0.91f }, 0.70f },
	{ "levels\\test\\dangercanyon\\bitmaps\\dangercanyon_arrow_white",
		"levels\\test\\sidewinder\\decals-environment\\arrow_white", { 42.01f, 44.17f, 0.13f }, { -0.42f, 0.91f }, 0.70f },
	{ "levels\\test\\dangercanyon\\bitmaps\\dangercanyon_arrow_blue",
		"levels\\test\\sidewinder\\decals-environment\\arrow_blue", { 51.11f, 24.66f, 0.13f }, { 0.42f, -0.91f }, 0.63f },
};
static struct gearbox_map_arrows const gearbox_sidewinder_arrows =
{
	/* (its painted stripes: drawn added, as Halo PC's arrows are) */
	"levels\\test\\sidewinder\\decals-environment\\fore_stripe_red",
	gearbox_sidewinder_arrow_list,
	(short)NUMBEROF(gearbox_sidewinder_arrow_list)
};

/* Sidewinder's teleporters to the bases, which Gearbox moved into rooms of
their own and framed: a doorway of two columns and a lintel, and a green
light behind the field */
static char const *const gearbox_sidewinder_frame_shaders[] =
{
	"levels\\b30\\shaders\\metal flat generic",
	"levels\\b30\\shaders\\metal strips wide",
	"levels\\test\\sidewinder\\shaders\\greenlight",
};
/* (each lit as the frame of the side teleporter on its side of the map,
whose shape, turned by the fields' ways, it is to within 0.03) */
static struct gearbox_map_frame const gearbox_sidewinder_frame_list[] =
{
	{ { -52.34f, 22.49f, 0.17f }, -2.01f, { -41.23f, 46.28f, 0.17f }, 1.14f },
	{ { 52.01f, 22.71f, 0.17f }, -1.13f, { 41.10f, 46.13f, 0.17f }, 2.01f },
};
/* (its columns, 0.24 wide beside a 0.72 doorway, and its lintel above it:
Halo PC's frame's corners, its front edges' bevel filled) */
static struct gearbox_frame_box const gearbox_sidewinder_frame_boxes[] =
{
	{ { -0.60f, -0.356f }, { -0.12f, 0.20f }, { -0.10f, 1.19f } },
	{ { 0.356f, 0.60f }, { -0.12f, 0.20f }, { -0.10f, 1.19f } },
	{ { -0.60f, 0.60f }, { -0.12f, 0.20f }, { 0.948f, 1.29f } },
};
static struct gearbox_map_frames const gearbox_sidewinder_frames =
{
	gearbox_sidewinder_frame_shaders,
	(short)NUMBEROF(gearbox_sidewinder_frame_shaders),
	1.6f,
	gearbox_sidewinder_frame_list,
	(short)NUMBEROF(gearbox_sidewinder_frame_list),
	gearbox_sidewinder_frame_boxes,
	(short)NUMBEROF(gearbox_sidewinder_frame_boxes),
	/* (the side frames' back is 0.22 in; the rooms' walls 0.20) */
	0.40f,
	0.18f
};

/* (Halo PC's structure bsps: Blood Gulch's small cliff walkable, Battle
Creek's side windows smaller. The flying ceilings: Blood Gulch's walls hide
what is outside it from eyes below 13.4 world units, at their lowest at the
red base's end, and Sidewinder's from eyes below 3.3, at the east wall
beside the blue Banshee's place. A Banshee's camera is at most 2.45 above
it while the top of its view is level or above (looking 28 degrees down, by
its camera track), and lower looking further down; so Blood Gulch's ceiling
is 2.75 below the walls. Sidewinder's is 1, as Gearbox's is, for a Banshee
(its spheres 0.08 below its origin) to rise to a base's upper floor (0.56):
its camera can then be 0.12 above the walls' lowest place. Gearbox's
ceilings, 11 and 1, pushed a Banshee down only a little, so that one
climbing hard flew well above them.) */
static struct gearbox_map_change const gearbox_map_changes[] =
{
	{ "bloodgulch", gearbox_bloodgulch_scenery, NULL, FALSE, TRUE, NULL, FALSE, NULL, NULL, TRUE, 10.5f },
	{ "beavercreek", NULL, NULL, FALSE, TRUE, NULL, FALSE, NULL, NULL, FALSE, 0.0f },
	/* (Sidewinder's own structure bsp: Halo PC's has its thick pillars,
	teleporter rooms and arrows, but not the Xbox's lightmaps' shadows of its
	trees and cliffs; its arrows are drawn on the Xbox's) */
	{ "sidewinder", gearbox_sidewinder_scenery, gearbox_sidewinder_equipment, TRUE, FALSE,
		gearbox_sidewinder_bsp_scenery, TRUE, &gearbox_sidewinder_arrows, &gearbox_sidewinder_frames, TRUE, 1.0f },
};

/* (how far below its flying ceiling a map's structure bsp starts pushing a
Banshee down, as Gearbox's ceilings do: it slows before it is held, and
climbing still reaches it) */
#define GEARBOX_SOFT_CEILING_DEPTH 0.5f

/* (decals.c's decal definition: the fields an arrow's sets) */
struct gearbox_decal_definition
{
	word flags;
	short type;
	short layer;
	word pad;
	struct tag_reference next_decal_in_chain;
	real radius_lower_bound;
	real radius_upper_bound;
	byte unused020[0xD8 - 0x20];
	struct tag_reference map;
	byte unused0E8[0x10C - 0xE8];
};

typedef char verify_gearbox_decal_definition_size[sizeof(struct gearbox_decal_definition) == 0x10C ? 1 : -1];
typedef char verify_gearbox_runtime_decal_size[sizeof(struct structure_runtime_decal) == 0x10 ? 1 : -1];

/* (how far above the floor an arrow's decal is cast from, and its cast's
pitch: down, but not straight down, so that its way is the arrow's) */
#define GEARBOX_ARROW_HEIGHT 0.25f
#define GEARBOX_ARROW_PITCH (-120)

/* the map's arrows: their decals' entries in the scenario's decal palette
(gearbox_change_apply), and the Xbox structure bsp's runtime decals with
them (gearbox_items_structure_bsp_loaded) */
static struct
{
	struct gearbox_map_arrows const *arrows;
	/* each arrow's decal (its index among the import's made tags), and its
	palette entry (NONE: none) */
	short made_indices[MAXIMUM_GEARBOX_ARROWS];
	short palette_indices[MAXIMUM_GEARBOX_ARROWS];
	struct structure_runtime_decal runtime_decals[MAXIMUM_GEARBOX_RUNTIME_DECALS];
} gearbox_map_arrows;

/* Halo PC's structure bsp of a map Gearbox changed: read to its own place
above the donor's tags, as a Halo PC map's is, its references made the grown
table's (gearbox_map_bsp_read), and drawn and collided with in the Xbox
map's place (scenario_structure_bsp_load, gearbox_items_structure_bsp) */
static struct
{
	boolean loaded;
	struct gearbox_structure_bsp_header *header;
	unsigned long size;
	boolean in_use;
} gearbox_map_bsp;

/* the changes Gearbox made to the map (its file's name), or NULL: none, or
game.gearbox_map_changes off */
static struct gearbox_map_change const *gearbox_map_change_find(
	char const *map_name)
{
	char const *name = map_name ? tag_name_strip_path(map_name) : NULL;
	short index;

	if (!name || !config_boolean("game.gearbox_map_changes"))
		return NULL;
	for (index = 0; index < (short)NUMBEROF(gearbox_map_changes); index++)
	{
		if (!csstrcasecmp(gearbox_map_changes[index].map_name, name))
			return &gearbox_map_changes[index];
	}

	return NULL;
}

/* whether the name is one of the list's (NULL: an empty list) */
static boolean gearbox_name_listed(
	char const *const *list,
	char const *name)
{
	for (; list && *list; list++)
	{
		if (name && !csstrcasecmp(*list, name))
			return TRUE;
	}

	return FALSE;
}

/* the name of the tag a handle of the map's table (the grown one, once it
is in place) names, or NULL */
static char const *gearbox_table_name(
	long handle)
{
	struct cache_file_tag_instance const *instances;
	long count, index;

	if (handle == NONE)
		return NULL;
	instances = cache_files_tag_instances(&count);
	index = DATUM_INDEX_TO_ABSOLUTE_INDEX(handle);

	return instances && index >= 0 && index < count && instances[index].tag_index == handle ?
		instances[index].name : NULL;
}

/* the name of the donor tag a handle of the donor's names, or NULL */
static char const *gearbox_donor_name(
	struct gearbox_import const *import,
	long handle)
{
	long index = gearbox_import_donor_index((struct gearbox_import *)import, handle);

	return index != NONE ? custom_edition_cache_tag_name(import->donor_tags, import->donor_bytes, index) : NULL;
}

/* the donor scenery placement's tag (absolute index), or NONE */
static long gearbox_change_scenery_tag(
	struct gearbox_import *import,
	struct scenario *donor_scenario,
	long placement_index)
{
	struct scenario_object_datum const *placement = gearbox_donor_element(import, &donor_scenario->scenery,
		placement_index, SCENERY_PLACEMENT_BYTES);
	struct scenario_object_palette_entry const *entry = placement ? gearbox_donor_element(import,
		&donor_scenario->scenery_palette, placement->palette_entry_index, sizeof(*entry)) : NULL;

	return entry ? gearbox_import_donor_index(import, entry->reference.index) : NONE;
}

/* The Halo PC map's placements of the map's changes chosen, before any
reference is rewritten (they name the donor's tags): FALSE if there are more
than room for. */
static boolean gearbox_change_select(
	struct gearbox_import *import,
	struct scenario *donor_scenario,
	struct gearbox_change_selection *selection)
{
	struct gearbox_map_change const *change = selection->change;
	long index;

	selection->scenery_count = 0;
	selection->equipment_count = 0;
	selection->flag_count = 0;
	for (index = 0; index < donor_scenario->scenery.count; index++)
	{
		long tag = gearbox_change_scenery_tag(import, donor_scenario, index);

		if (tag == NONE || !gearbox_name_listed(change->scenery,
			custom_edition_cache_tag_name(import->donor_tags, import->donor_bytes, tag)))
		{
			continue;
		}
		if (selection->scenery_count >= MAXIMUM_GEARBOX_PLACEMENTS)
			return FALSE;
		selection->scenery[selection->scenery_count++] = index;
	}
	for (index = 0; index < donor_scenario->netgame_equipment.count; index++)
	{
		struct scenario_netgame_equipment const *placement = gearbox_donor_element(import,
			&donor_scenario->netgame_equipment, index, sizeof(*placement));

		if (!placement || !gearbox_name_listed(change->equipment,
			gearbox_donor_name(import, placement->item_collection.index)))
		{
			continue;
		}
		if (selection->equipment_count >= MAXIMUM_GEARBOX_PLACEMENTS)
			return FALSE;
		selection->equipment[selection->equipment_count++] = index;
	}
	for (index = 0; change->teleporters && index < donor_scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag const *flag = gearbox_donor_element(import, &donor_scenario->netgame_flags,
			index, sizeof(*flag));

		if (!flag || (flag->type != _netgame_flag_teleporter_source && flag->type != _netgame_flag_teleporter_target))
			continue;
		if (selection->flag_count >= MAXIMUM_GEARBOX_PLACEMENTS)
			return FALSE;
		selection->flags[selection->flag_count++] = index;
	}

	return TRUE;
}

/* the chosen placements' tags imported (with those they name) */
static void gearbox_change_resolve(
	struct gearbox_import *import,
	struct scenario *donor_scenario,
	struct gearbox_change_selection const *selection)
{
	long index;

	for (index = 0; index < selection->scenery_count; index++)
	{
		long tag = gearbox_change_scenery_tag(import, donor_scenario, selection->scenery[index]);

		if (tag != NONE)
			gearbox_import_resolve(import, tag);
	}
	for (index = 0; index < selection->equipment_count; index++)
	{
		struct scenario_netgame_equipment const *placement = gearbox_donor_element(import,
			&donor_scenario->netgame_equipment, selection->equipment[index], sizeof(*placement));
		long collection = gearbox_import_donor_index(import, placement->item_collection.index);

		if (collection != NONE)
			gearbox_import_resolve(import, collection);
	}

	return;
}

/* The decals of the map's arrows made, each a copy of the Xbox map's decal
with the Halo PC map's bitmap of its arrow (imported), its tags to be added
to the grown table. An arrow whose decal cannot be is not drawn. */
static void gearbox_change_arrows_make(
	struct gearbox_import *import,
	struct gearbox_map_arrows const *arrows)
{
	struct cache_file_tag_instance const *template_instance = NULL;
	short arrow_index;
	long index;

	for (index = 0; index < import->xbox_count; index++)
	{
		struct cache_file_tag_instance const *instance = &import->xbox_instances[index];

		if ((unsigned long)instance->group_tag == DECAL_GROUP_TAG && instance->name && instance->base_address &&
			!csstrcasecmp(instance->name, arrows->template_decal))
		{
			template_instance = instance;
		}
	}
	if (!template_instance || arrows->count > MAXIMUM_GEARBOX_ARROWS)
	{
		error(_error_silent, "gearbox items: the map's arrows are not drawn: it has no decal '%s'", arrows->template_decal);
		return;
	}
	gearbox_map_arrows.arrows = arrows;
	for (arrow_index = 0; arrow_index < arrows->count; arrow_index++)
	{
		struct gearbox_map_arrow const *arrow = &arrows->arrows[arrow_index];
		long bitmap = gearbox_import_donor_tag(import, GEARBOX_BITMAP_TAG, arrow->bitmap);
		struct gearbox_decal_definition *definition;
		long handle;
		short made;

		gearbox_map_arrows.made_indices[arrow_index] = NONE;
		gearbox_map_arrows.palette_indices[arrow_index] = NONE;
		/* (one decal of each name) */
		for (made = 0; made < import->made_count && csstrcasecmp(import->made[made].name, arrow->decal); made++)
			;
		if (made < import->made_count)
		{
			gearbox_map_arrows.made_indices[arrow_index] = made;
			continue;
		}
		handle = bitmap != NONE ? gearbox_import_resolve(import, bitmap) : NONE;
		definition = handle != NONE && import->made_count < MAXIMUM_GEARBOX_MADE_TAGS ?
			gearbox_allocate(sizeof(*definition)) : NULL;
		if (!definition || import->xbox_count + import->added_count >= UNSIGNED_SHORT_MAX ||
			import->first_salt + import->added_count >= UNSIGNED_SHORT_MAX)
		{
			error(_error_silent, "gearbox items: the arrow '%s' is not drawn: %s", arrow->decal,
				bitmap == NONE ? "the Halo PC map has not its bitmap" : "there is no room for its decal");
			continue;
		}
		/* (painted on the surface it is cast on, turned the way of its cast,
		of the size of Halo PC's) */
		memcpy(definition, template_instance->base_address, sizeof(*definition));
		definition->flags = FLAG(_decal_definition_no_random_rotation_bit) | FLAG(_decal_definition_preserve_aspect_bit);
		definition->next_decal_in_chain.index = NONE;
		definition->radius_lower_bound = arrow->radius;
		definition->radius_upper_bound = arrow->radius;
		definition->map.group_tag = GEARBOX_BITMAP_TAG;
		definition->map.name = (char *)custom_edition_cache_tag_name(import->donor_tags, import->donor_bytes, bitmap);
		definition->map.name_length = (long)strlen(definition->map.name);
		definition->map.index = handle;
		import->made[import->made_count] = *template_instance;
		import->made[import->made_count].name = (char *)arrow->decal;
		import->made[import->made_count].base_address = definition;
		import->made[import->made_count].tag_index = ((import->first_salt + import->added_count) << 16) |
			(import->xbox_count + import->added_count);
		import->added_count++;
		gearbox_map_arrows.made_indices[arrow_index] = import->made_count++;
	}

	return;
}

/* the map's arrows' decals given entries in the scenario's decal palette,
once the grown table is in place */
static void gearbox_change_arrows_apply(
	struct gearbox_import const *import,
	struct scenario *scenario)
{
	struct gearbox_map_arrows const *arrows = gearbox_map_arrows.arrows;
	struct scenario_decal_palette_entry *palette;
	long count = scenario->decal_palette.count;
	short arrow_index, made;

	if (!arrows || !import->made_count)
		return;
	/* (a runtime decal's palette index is a byte) */
	palette = count >= 0 && count + import->made_count <= UNSIGNED_CHAR_MAX ?
		gearbox_allocate((count + import->made_count) * sizeof(*palette)) : NULL;
	if (!palette || gearbox_items_globals.patch_count >= MAXIMUM_GEARBOX_PATCHES)
	{
		error(_error_silent, "gearbox items: the map's arrows are not drawn: no room in its decal palette");
		gearbox_map_arrows.arrows = NULL;
		return;
	}
	if (count > 0)
		memcpy(palette, scenario->decal_palette.address, count * sizeof(*palette));
	for (made = 0; made < import->made_count; made++)
	{
		palette[count + made].reference.group_tag = DECAL_GROUP_TAG;
		palette[count + made].reference.name = import->made[made].name;
		palette[count + made].reference.name_length = (long)strlen(import->made[made].name);
		palette[count + made].reference.index = import->made[made].tag_index;
	}
	for (arrow_index = 0; arrow_index < arrows->count; arrow_index++)
	{
		made = gearbox_map_arrows.made_indices[arrow_index];
		gearbox_map_arrows.palette_indices[arrow_index] = made != NONE ? (short)(count + made) : NONE;
	}
	gearbox_block_replace(&scenario->decal_palette, count + import->made_count, palette);

	return;
}

/* ---- Sidewinder's teleporter frames (gearbox_map_frames) */

/* Halo PC's triangles taken for the frames, the Xbox map's near them, and
the materials made of them */
#define MAXIMUM_GEARBOX_FRAME_TRIANGLES 256
#define MAXIMUM_GEARBOX_FRAME_NEARBY 2048
#define MAXIMUM_GEARBOX_FRAME_GROUPS 16
#define MAXIMUM_GEARBOX_FRAMES 8
/* the frames' boxes, and the collision bsp's leaves they split */
#define MAXIMUM_GEARBOX_FRAME_BOXES 16
#define MAXIMUM_GEARBOX_FRAME_SLOTS 512
/* (a triangle with a corner this far above a teleporter's floor rises above
it; one whose corners are all this near the Xbox map's surfaces lies on
them; and the Xbox map's surfaces this much further than the frames' are
the room's) */
#define GEARBOX_FRAME_FLOOR 0.08f
#define GEARBOX_FRAME_ON_SURFACE 0.01f
#define GEARBOX_FRAME_NEARBY 2.0f
/* (how near Halo PC's frame the Xbox frame's triangles copied onto it lie:
the frames are of one shape to within 0.03) */
#define GEARBOX_FRAME_COPY_NEAR 0.04f

/* (rasterizer_geometry.c's uncompressed vertices) */
struct gearbox_environment_vertex
{
	real_point3d position;
	real_vector3d normal;
	real_vector3d binormal;
	real_vector3d tangent;
	real_point2d texcoord;
};

struct gearbox_lightmap_vertex
{
	real_vector3d incident_radiosity;
	real_point2d texcoord;
};

/* (structure_visibility.c's cluster and subcluster) */
struct gearbox_cluster_view
{
	byte unused[0x34];
	struct tag_block subclusters;
	byte unused40[0x28];
};

struct gearbox_subcluster
{
	real_rectangle3d world_bounds;
	struct tag_block surface_indices;
};

typedef char verify_gearbox_environment_vertex_size[sizeof(struct gearbox_environment_vertex) == 56 ? 1 : -1];
typedef char verify_gearbox_lightmap_vertex_size[sizeof(struct gearbox_lightmap_vertex) == 20 ? 1 : -1];
typedef char verify_gearbox_cluster_view_size[sizeof(struct gearbox_cluster_view) == 0x68 ? 1 : -1];
typedef char verify_gearbox_subcluster_size[sizeof(struct gearbox_subcluster) == 0x24 ? 1 : -1];

/* a triangle of Halo PC's frames taken (its frame and shader, and the
incident light of its corners' lightmap vertices), or of the Xbox map's own
frame copied onto it, with its lightmap places (gearbox_frames_copy) */
struct gearbox_frame_triangle
{
	short frame;
	short shader;
	struct gearbox_environment_vertex vertices[3];
	real_vector3d incident[3];
	boolean copied;
	real_point2d lightmap_texcoords[3];
};

/* a triangle of the Xbox map's near a frame (or the frame it is lit as):
its lightmap (NONE: unlit) and its corners' lightmap places */
struct gearbox_frame_nearby
{
	short frame;
	boolean lit_source;
	short lightmap;
	real_point3d corners[3];
	real_point2d lightmap_texcoords[3];
	/* (its material, and its corners' vertices in it) */
	struct structure_material const *material;
	word vertex_indices[3];
};

/* what the frames were drawn with on the structure bsp loaded last: let go
when another is loaded, or the map's tags go */
static struct
{
	void *blocks;
	byte *vertices;
	boolean vertices_contiguous;
	struct structure_material *materials;
	short material_count;
	/* (the collision bsp's blocks the frames' boxes join) */
	void *collision[2];
} gearbox_frames_made;

static struct gearbox_frame_triangle gearbox_frame_triangles[MAXIMUM_GEARBOX_FRAME_TRIANGLES];
static struct gearbox_frame_nearby gearbox_frames_nearby[MAXIMUM_GEARBOX_FRAME_NEARBY];
static struct gearbox_environment_vertex gearbox_frame_vertices[3 * MAXIMUM_GEARBOX_FRAME_TRIANGLES];
static struct gearbox_lightmap_vertex gearbox_frame_lightmap_vertices[3 * MAXIMUM_GEARBOX_FRAME_TRIANGLES];

static void gearbox_frames_release(
	void)
{
	short index;

	for (index = 0; index < gearbox_frames_made.material_count; index++)
	{
		rasterizer_vertex_buffer_delete(&gearbox_frames_made.materials[index].vertices);
		rasterizer_vertex_buffer_delete(&gearbox_frames_made.materials[index].lightmap_vertices);
	}
	if (gearbox_frames_made.vertices)
	{
		if (gearbox_frames_made.vertices_contiguous)
			XPhysicalFree(gearbox_frames_made.vertices);
		else
			free(gearbox_frames_made.vertices);
	}
	if (gearbox_frames_made.blocks)
		free(gearbox_frames_made.blocks);
	if (gearbox_frames_made.collision[0])
		free(gearbox_frames_made.collision[0]);
	if (gearbox_frames_made.collision[1])
		free(gearbox_frames_made.collision[1]);
	memset(&gearbox_frames_made, 0, sizeof(gearbox_frames_made));

	return;
}

/* `count` elements of `size` bytes at an address of Halo PC's structure bsp
(gearbox_barriers), or NULL: not all of them were read */
static void const *gearbox_pc_bsp_elements(
	void const *address,
	long count,
	long size)
{
	unsigned long offset = (unsigned long)address - gearbox_barriers.base;

	if (!gearbox_barriers.data || count < 0 || size <= 0 || (unsigned long)address < gearbox_barriers.base ||
		offset > gearbox_barriers.size || (unsigned long)count > (gearbox_barriers.size - offset) / (unsigned long)size)
	{
		return NULL;
	}

	return gearbox_barriers.data + offset;
}

/* the name of the donor's tag a handle names, or NULL */
static char const *gearbox_donor_handle_name(
	long handle)
{
	unsigned long donor_bytes;
	byte *donor_tags = custom_edition_cache_donor_tags(&donor_bytes);
	struct gearbox_tag_header const *donor = gearbox_barriers.donor;
	long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(handle);

	if (!donor_tags || !donor || handle == NONE || index >= donor->tag_count ||
		donor->instances[index].tag_index != handle)
	{
		return NULL;
	}

	return custom_edition_cache_tag_name(donor_tags, donor_bytes, index);
}

/* the handle of the map's shader of the name (in the grown table), or NONE */
static long gearbox_shader_handle(
	char const *name)
{
	struct cache_file_tag_instance const *instances;
	long count, index;

	instances = cache_files_tag_instances(&count);
	for (index = 0; instances && index < count; index++)
	{
		struct cache_file_tag_instance const *instance = &instances[index];

		if (instance->name && instance->base_address && !csstrcasecmp(instance->name, name) &&
			(instance->group_tag == (long)GEARBOX_SHADER_TAG || instance->parent_group_tags[0] == (long)GEARBOX_SHADER_TAG ||
				instance->parent_group_tags[1] == (long)GEARBOX_SHADER_TAG))
		{
			return instance->tag_index;
		}
	}

	return NONE;
}

/* the distance squared from `point` to the nearest point of the triangle,
and its corners' weights that make that point */
static real gearbox_triangle_nearest(
	real_point3d const *point,
	real_point3d const *corners,
	real *weights)
{
	real const *a = corners[0].n, *b = corners[1].n, *c = corners[2].n, *p = point->n;
	real ab[3], ac[3], ap[3], bp[3], cp[3];
	real d1 = 0.0f, d2 = 0.0f, d3 = 0.0f, d4 = 0.0f, d5 = 0.0f, d6 = 0.0f;
	real va, vb, vc, distance = 0.0f;
	short k;

	for (k = 0; k < 3; k++)
	{
		ab[k] = b[k] - a[k];
		ac[k] = c[k] - a[k];
		ap[k] = p[k] - a[k];
		bp[k] = p[k] - b[k];
		cp[k] = p[k] - c[k];
	}
	for (k = 0; k < 3; k++)
	{
		d1 += ab[k] * ap[k];
		d2 += ac[k] * ap[k];
		d3 += ab[k] * bp[k];
		d4 += ac[k] * bp[k];
		d5 += ab[k] * cp[k];
		d6 += ac[k] * cp[k];
	}
	va = d3 * d6 - d5 * d4;
	vb = d5 * d2 - d1 * d6;
	vc = d1 * d4 - d3 * d2;
	weights[0] = weights[1] = weights[2] = 0.0f;
	if (d1 <= 0.0f && d2 <= 0.0f)
	{
		weights[0] = 1.0f;
	}
	else if (d3 >= 0.0f && d4 <= d3)
	{
		weights[1] = 1.0f;
	}
	else if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
	{
		weights[1] = d1 - d3 != 0.0f ? d1 / (d1 - d3) : 0.5f;
		weights[0] = 1.0f - weights[1];
	}
	else if (d6 >= 0.0f && d5 <= d6)
	{
		weights[2] = 1.0f;
	}
	else if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
	{
		weights[2] = d2 - d6 != 0.0f ? d2 / (d2 - d6) : 0.5f;
		weights[0] = 1.0f - weights[2];
	}
	else if (va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f)
	{
		weights[2] = (d4 - d3) + (d5 - d6) != 0.0f ? (d4 - d3) / ((d4 - d3) + (d5 - d6)) : 0.5f;
		weights[1] = 1.0f - weights[2];
	}
	else if (va + vb + vc != 0.0f)
	{
		weights[1] = vb / (va + vb + vc);
		weights[2] = vc / (va + vb + vc);
		weights[0] = 1.0f - weights[1] - weights[2];
	}
	else
	{
		weights[0] = 1.0f;
	}
	for (k = 0; k < 3; k++)
	{
		real nearest = a[k] * weights[0] + b[k] * weights[1] + c[k] * weights[2];

		distance += (p[k] - nearest) * (p[k] - nearest);
	}

	return distance;
}

/* the Xbox map's triangles near the frames and the frames they are lit as,
into gearbox_frames_nearby */
static long gearbox_frames_gather_nearby(
	struct structure_bsp const *structure_bsp,
	struct gearbox_map_frames const *frames)
{
	long vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_environment_compressed);
	long lightmap_vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_environment_lightmap_compressed);
	word const *surfaces = (word const *)structure_bsp->surfaces.address;
	long count = 0;
	long lightmap_index;

	for (lightmap_index = 0; lightmap_index < structure_bsp->lightmaps.count; lightmap_index++)
	{
		struct structure_lightmap const *lightmap = TAG_BLOCK_GET_ELEMENT(&structure_bsp->lightmaps, lightmap_index,
			struct structure_lightmap);
		long material_index;

		for (material_index = 0; material_index < lightmap->materials.count; material_index++)
		{
			struct structure_material const *material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, material_index,
				struct structure_material);
			byte const *vertices = (byte const *)material->compressed_vertex_data.address;
			long vertex_count = material->vertices.count;
			long lightmap_count = material->lightmap_vertices.count;
			boolean lit = lightmap->bitmap_index != NONE && lightmap_count == vertex_count;
			long surface_index;

			if (!vertices || vertex_count <= 0 || lightmap_count < 0 || vertex_count > 0x10000 ||
				material->compressed_vertex_data.size < vertex_count * vertex_size + lightmap_count * lightmap_vertex_size ||
				material->first_surface_index < 0 || material->surface_count < 0 ||
				material->first_surface_index > structure_bsp->surfaces.count - material->surface_count)
			{
				continue;
			}
			for (surface_index = material->first_surface_index;
				surface_index < material->first_surface_index + material->surface_count;
				surface_index++)
			{
				word const *corner_indices = surfaces + 3 * surface_index;
				struct gearbox_frame_nearby *nearby = &gearbox_frames_nearby[count];
				real x = 0.0f, y = 0.0f;
				short corner, frame;

				if (corner_indices[0] >= vertex_count || corner_indices[1] >= vertex_count || corner_indices[2] >= vertex_count)
					continue;
				for (corner = 0; corner < 3; corner++)
				{
					memcpy(&nearby->corners[corner], vertices + corner_indices[corner] * vertex_size, sizeof(real_point3d));
					x += nearby->corners[corner].x / 3.0f;
					y += nearby->corners[corner].y / 3.0f;
				}
				for (frame = 0; frame < 2 * frames->count; frame++)
				{
					real_point3d const *place = frame < frames->count ? &frames->frames[frame].position :
						&frames->frames[frame - frames->count].lit_position;
					real dx = x - place->x, dy = y - place->y;

					if (dx * dx + dy * dy <= (frames->radius + GEARBOX_FRAME_NEARBY) * (frames->radius + GEARBOX_FRAME_NEARBY))
						break;
				}
				if (frame == 2 * frames->count)
					continue;
				nearby->lit_source = frame >= frames->count;
				nearby->frame = frame % frames->count;
				nearby->material = material;
				nearby->vertex_indices[0] = corner_indices[0];
				nearby->vertex_indices[1] = corner_indices[1];
				nearby->vertex_indices[2] = corner_indices[2];
				nearby->lightmap = lit ? (short)lightmap_index : NONE;
				for (corner = 0; lit && corner < 3; corner++)
				{
					short texcoord[2];

					memcpy(texcoord, vertices + vertex_count * vertex_size + corner_indices[corner] * lightmap_vertex_size + 4,
						sizeof(texcoord));
					nearby->lightmap_texcoords[corner].x = ((real)texcoord[0] + 0.5f) / 32767.5f;
					nearby->lightmap_texcoords[corner].y = ((real)texcoord[1] + 0.5f) / 32767.5f;
				}
				if (++count == MAXIMUM_GEARBOX_FRAME_NEARBY)
					return count;
			}
		}
	}

	return count;
}

/* whether all the triangle's corners lie on the Xbox map's surfaces near
the frame */
static boolean gearbox_frame_triangle_on_xbox(
	struct gearbox_frame_triangle const *triangle,
	long nearby_count)
{
	short corner;

	for (corner = 0; corner < 3; corner++)
	{
		long index;

		for (index = 0; index < nearby_count; index++)
		{
			real weights[3];

			if (gearbox_frames_nearby[index].frame == triangle->frame && !gearbox_frames_nearby[index].lit_source &&
				gearbox_triangle_nearest(&triangle->vertices[corner].position, gearbox_frames_nearby[index].corners, weights) <
					GEARBOX_FRAME_ON_SURFACE * GEARBOX_FRAME_ON_SURFACE)
			{
				break;
			}
		}
		if (index == nearby_count)
			return FALSE;
	}

	return TRUE;
}

/* Halo PC's triangles of the frames, into gearbox_frame_triangles */
static long gearbox_frames_pick(
	struct gearbox_map_frames const *frames,
	long nearby_count)
{
	struct structure_bsp const *pc = gearbox_barriers.structure_bsp;
	struct structure_lightmap const *lightmaps = pc ?
		gearbox_pc_bsp_elements(pc->lightmaps.address, pc->lightmaps.count, sizeof(*lightmaps)) : NULL;
	word const *surfaces = pc ? gearbox_pc_bsp_elements(pc->surfaces.address, pc->surfaces.count, 3 * sizeof(word)) : NULL;
	long count = 0;
	long lightmap_index;

	for (lightmap_index = 0; lightmaps && surfaces && lightmap_index < pc->lightmaps.count; lightmap_index++)
	{
		struct structure_material const *materials = gearbox_pc_bsp_elements(lightmaps[lightmap_index].materials.address,
			lightmaps[lightmap_index].materials.count, sizeof(*materials));
		long material_index;

		for (material_index = 0; materials && material_index < lightmaps[lightmap_index].materials.count; material_index++)
		{
			struct structure_material const *material = &materials[material_index];
			char const *name = gearbox_donor_handle_name(material->shader.index);
			long vertex_count = material->vertices.count;
			long lightmap_count = material->lightmap_vertices.count;
			struct gearbox_environment_vertex const *vertices = NULL;
			struct gearbox_lightmap_vertex const *lightmap_vertices = NULL;
			long surface_index;
			short shader;

			for (shader = 0; name && shader < frames->shader_count && csstrcasecmp(frames->shaders[shader], name); shader++)
				;
			if (!name || shader == frames->shader_count || vertex_count <= 0 || vertex_count > 0x10000 ||
				(lightmap_count != 0 && lightmap_count != vertex_count) ||
				material->uncompressed_vertex_data.size < vertex_count * (long)sizeof(*vertices) +
					lightmap_count * (long)sizeof(*lightmap_vertices) ||
				material->first_surface_index < 0 || material->surface_count < 0 ||
				material->first_surface_index > pc->surfaces.count - material->surface_count)
			{
				continue;
			}
			vertices = gearbox_pc_bsp_elements(material->uncompressed_vertex_data.address,
				vertex_count * (long)sizeof(*vertices) + lightmap_count * (long)sizeof(*lightmap_vertices), 1);
			if (!vertices)
				continue;
			if (lightmap_count)
				lightmap_vertices = (struct gearbox_lightmap_vertex const *)(vertices + vertex_count);
			for (surface_index = material->first_surface_index;
				surface_index < material->first_surface_index + material->surface_count;
				surface_index++)
			{
				word const *corner_indices = surfaces + 3 * surface_index;
				struct gearbox_frame_triangle *triangle = &gearbox_frame_triangles[count];
				real x = 0.0f, y = 0.0f, top = -1.0e6f;
				short corner, frame;

				if (corner_indices[0] >= vertex_count || corner_indices[1] >= vertex_count || corner_indices[2] >= vertex_count)
					continue;
				for (corner = 0; corner < 3; corner++)
				{
					triangle->vertices[corner] = vertices[corner_indices[corner]];
					triangle->incident[corner] = lightmap_vertices ? lightmap_vertices[corner_indices[corner]].incident_radiosity :
						vertices[corner_indices[corner]].normal;
					x += triangle->vertices[corner].position.x / 3.0f;
					y += triangle->vertices[corner].position.y / 3.0f;
					if (triangle->vertices[corner].position.z > top)
						top = triangle->vertices[corner].position.z;
				}
				for (frame = 0; frame < frames->count; frame++)
				{
					real_point3d const *place = &frames->frames[frame].position;
					real dx = x - place->x, dy = y - place->y;

					if (dx * dx + dy * dy <= frames->radius * frames->radius && top > place->z + GEARBOX_FRAME_FLOOR)
					{
						break;
					}
				}
				if (frame == frames->count)
					continue;
				triangle->frame = frame;
				triangle->shader = shader;
				triangle->copied = FALSE;
				if (gearbox_frame_triangle_on_xbox(triangle, nearby_count))
					continue;
				if (++count == MAXIMUM_GEARBOX_FRAME_TRIANGLES)
					return count;
			}
		}
	}

	return count;
}

/* the lightmap place for a point of a frame: of the wall behind it (its
lightmap's triangle `wall`), at the wall's point nearest it, a fifth of the
way to the triangle's middle. (One triangle's: the nearest surface's place
for each corner put a face's corners in different parts of the lightmap,
whose texels between them are other surfaces'.) */
static real_point2d gearbox_frame_lightmap_texcoord(
	real_point3d const *point,
	struct gearbox_frame_nearby const *wall)
{
	real_point2d texcoord;
	real weights[3];
	short corner;

	gearbox_triangle_nearest(point, wall->corners, weights);
	texcoord.x = texcoord.y = 0.0f;
	for (corner = 0; corner < 3; corner++)
	{
		real weight = weights[corner] * 0.8f + 0.2f / 3.0f;

		texcoord.x += wall->lightmap_texcoords[corner].x * weight;
		texcoord.y += wall->lightmap_texcoords[corner].y * weight;
	}

	return texcoord;
}

/* the lightmap place for a corner of a frame, its face's normal `normal`,
lit as the Xbox map's own frame of the same shape is (gearbox_map_frame):
the corner turned and moved onto that frame, at the nearest point of its
surfaces facing the same way (any of them, none facing so) in its lightmap
`lightmap`, a little way to its triangle's middle */
static real_point2d gearbox_frame_lit_texcoord(
	struct gearbox_map_frame const *definition,
	short frame,
	short lightmap,
	long nearby_count,
	real_point3d const *point,
	real_vector3d const *normal)
{
	real turn = definition->lit_yaw - definition->yaw;
	real c = cosine(turn), s = sine(turn);
	real dx = point->x - definition->position.x, dy = point->y - definition->position.y;
	real best_distance = -1.0f, any_distance = -1.0f;
	real best_weights[3], any_weights[3];
	long best = NONE, any = NONE, index;
	real_point3d moved;
	real_vector3d turned;
	real_point2d texcoord;
	short corner;

	moved.x = definition->lit_position.x + c * dx - s * dy;
	moved.y = definition->lit_position.y + s * dx + c * dy;
	moved.z = point->z - definition->position.z + definition->lit_position.z;
	turned.i = c * normal->i - s * normal->j;
	turned.j = s * normal->i + c * normal->j;
	turned.k = normal->k;
	for (index = 0; index < nearby_count; index++)
	{
		struct gearbox_frame_nearby const *nearby = &gearbox_frames_nearby[index];
		real_point3d const *q = nearby->corners;
		real nx = (q[1].y - q[0].y) * (q[2].z - q[0].z) - (q[1].z - q[0].z) * (q[2].y - q[0].y);
		real ny = (q[1].z - q[0].z) * (q[2].x - q[0].x) - (q[1].x - q[0].x) * (q[2].z - q[0].z);
		real nz = (q[1].x - q[0].x) * (q[2].y - q[0].y) - (q[1].y - q[0].y) * (q[2].x - q[0].x);
		real along = nx * turned.i + ny * turned.j + nz * turned.k;
		real weights[3];
		real distance;

		if (nearby->frame != frame || !nearby->lit_source || nearby->lightmap != lightmap)
			continue;
		distance = gearbox_triangle_nearest(&moved, nearby->corners, weights);
		if (any == NONE || distance < any_distance)
		{
			any = index;
			any_distance = distance;
			any_weights[0] = weights[0];
			any_weights[1] = weights[1];
			any_weights[2] = weights[2];
		}
		/* (facing the same way: within some 25 degrees) */
		if (along > 0.0f && along * along > 0.81f * (nx * nx + ny * ny + nz * nz) &&
			(best == NONE || distance < best_distance))
		{
			best = index;
			best_distance = distance;
			best_weights[0] = weights[0];
			best_weights[1] = weights[1];
			best_weights[2] = weights[2];
		}
	}
	if (best == NONE)
	{
		best = any;
		best_weights[0] = any_weights[0];
		best_weights[1] = any_weights[1];
		best_weights[2] = any_weights[2];
	}
	texcoord.x = texcoord.y = 0.0f;
	for (corner = 0; best != NONE && corner < 3; corner++)
	{
		real weight = best_weights[corner] * 0.96f + 0.04f / 3.0f;

		texcoord.x += gearbox_frames_nearby[best].lightmap_texcoords[corner].x * weight;
		texcoord.y += gearbox_frames_nearby[best].lightmap_texcoords[corner].y * weight;
	}

	return texcoord;
}

/* The Xbox map's own frames' lit triangles that the frames are lit as
(gearbox_map_frame), copied onto them: those of its frame's shaders in the
lightmap `lit_lightmaps` names, turned and moved onto the frame, whose
corners all lie on Halo PC's triangles of the frame (gearbox_frames_pick),
and its doorway's back, brought in front of the wall it stands at, with
their own texture and lightmap places, after those in
gearbox_frame_triangles. (Halo PC's are cut otherwise: the lightmap places
of the Xbox frame's surfaces at their corners are of different parts of the
lightmap, and a face's texels between them are other surfaces'.) */
static long gearbox_frames_copy(
	struct gearbox_map_frames const *frames,
	short const *lit_lightmaps,
	long const *shader_handles,
	long triangle_count,
	long nearby_count,
	boolean *copied)
{
	long vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_environment_compressed);
	long lightmap_vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_environment_lightmap_compressed);
	long picked_count = triangle_count, index;
	short frame;

	for (frame = 0; frame < frames->count; frame++)
		copied[frame] = FALSE;
	for (index = 0; index < nearby_count && triangle_count < MAXIMUM_GEARBOX_FRAME_TRIANGLES; index++)
	{
		struct gearbox_frame_nearby const *nearby = &gearbox_frames_nearby[index];
		struct structure_material const *material = nearby->material;
		struct gearbox_frame_triangle *triangle = &gearbox_frame_triangles[triangle_count];
		struct gearbox_map_frame const *definition;
		byte const *vertices;
		real turn, c, s;
		short shader, corner;

		if (!nearby->lit_source || nearby->lightmap == NONE || nearby->lightmap != lit_lightmaps[nearby->frame] ||
			!material || material->lightmap_vertices.count != material->vertices.count)
		{
			continue;
		}
		for (shader = 0; shader < frames->shader_count && shader_handles[shader] != material->shader.index; shader++)
			;
		if (shader == frames->shader_count)
			continue;
		definition = &frames->frames[nearby->frame];
		turn = definition->yaw - definition->lit_yaw;
		c = cosine(turn);
		s = sine(turn);
		vertices = (byte const *)material->compressed_vertex_data.address;
		for (corner = 0; corner < 3; corner++)
		{
			byte const *vertex = vertices + nearby->vertex_indices[corner] * vertex_size;
			byte const *lightmap_vertex = vertices + material->vertices.count * vertex_size +
				nearby->vertex_indices[corner] * lightmap_vertex_size;
			struct gearbox_environment_vertex *made = &triangle->vertices[corner];
			real_vector3d *vectors[4];
			unsigned long packed[4];
			real_point3d position;
			short texcoord[2];
			short vector;
			real dx, dy;

			memcpy(&position, vertex, sizeof(position));
			memcpy(packed, vertex + 12, 3 * sizeof(unsigned long));
			memcpy(&made->texcoord, vertex + 24, sizeof(made->texcoord));
			memcpy(&packed[3], lightmap_vertex, sizeof(unsigned long));
			memcpy(texcoord, lightmap_vertex + 4, sizeof(texcoord));
			dx = position.x - definition->lit_position.x;
			dy = position.y - definition->lit_position.y;
			made->position.x = definition->position.x + c * dx - s * dy;
			made->position.y = definition->position.y + s * dx + c * dy;
			made->position.z = position.z - definition->lit_position.z + definition->position.z;
			vectors[0] = &made->normal;
			vectors[1] = &made->binormal;
			vectors[2] = &made->tangent;
			vectors[3] = &triangle->incident[corner];
			for (vector = 0; vector < 4; vector++)
			{
				real_vector3d unpacked = uncompress_int32_to_real_vector3d(packed[vector]);

				vectors[vector]->i = c * unpacked.i - s * unpacked.j;
				vectors[vector]->j = s * unpacked.i + c * unpacked.j;
				vectors[vector]->k = unpacked.k;
			}
			triangle->lightmap_texcoords[corner].x = ((real)texcoord[0] + 0.5f) / 32767.5f;
			triangle->lightmap_texcoords[corner].y = ((real)texcoord[1] + 0.5f) / 32767.5f;
		}
		/* (the doorway's back: within the doorway, behind the frame's front,
		drawn at back_depth) */
		{
			real forward_x = cosine(definition->yaw), forward_y = sine(definition->yaw);
			real deepest = -1.0e6f;

			for (corner = 0; corner < 3; corner++)
			{
				real px = triangle->vertices[corner].position.x - definition->position.x;
				real py = triangle->vertices[corner].position.y - definition->position.y;
				real across = -forward_y * px + forward_x * py, depth = forward_x * px + forward_y * py;

				if (across > frames->doorway || across < -frames->doorway || depth < frames->back_depth - 0.1f)
					break;
				deepest = MAX(deepest, depth);
			}
			if (corner == 3)
			{
				for (corner = 0; corner < 3; corner++)
				{
					triangle->vertices[corner].position.x -= forward_x * (deepest - frames->back_depth);
					triangle->vertices[corner].position.y -= forward_y * (deepest - frames->back_depth);
				}
				triangle->frame = nearby->frame;
				triangle->shader = shader;
				triangle->copied = TRUE;
				copied[nearby->frame] = TRUE;
				triangle_count++;
				continue;
			}
		}
		/* (on Halo PC's frame: its corners, middle and edges' middles each on
		one of its triangles; one with its corners on the frame's edges may
		cross the doorway) */
		for (corner = 0; corner < 7; corner++)
		{
			real_point3d const *a = &triangle->vertices[corner % 3].position;
			real_point3d const *b = &triangle->vertices[(corner + 1) % 3].position;
			real_point3d const *d = &triangle->vertices[(corner + 2) % 3].position;
			real_point3d point;
			long picked;

			if (corner < 3)
			{
				point = *a;
			}
			else if (corner < 6)
			{
				point.x = (a->x + b->x) / 2.0f;
				point.y = (a->y + b->y) / 2.0f;
				point.z = (a->z + b->z) / 2.0f;
			}
			else
			{
				point.x = (a->x + b->x + d->x) / 3.0f;
				point.y = (a->y + b->y + d->y) / 3.0f;
				point.z = (a->z + b->z + d->z) / 3.0f;
			}
			for (picked = 0; picked < picked_count; picked++)
			{
				struct gearbox_frame_triangle const *other = &gearbox_frame_triangles[picked];
				real_point3d other_corners[3];
				real weights[3];

				if (other->frame != nearby->frame)
					continue;
				other_corners[0] = other->vertices[0].position;
				other_corners[1] = other->vertices[1].position;
				other_corners[2] = other->vertices[2].position;
				if (gearbox_triangle_nearest(&point, other_corners, weights) <
					GEARBOX_FRAME_COPY_NEAR * GEARBOX_FRAME_COPY_NEAR)
				{
					break;
				}
			}
			if (picked == picked_count)
				break;
		}
		if (corner < 7)
			continue;
		triangle->frame = nearby->frame;
		triangle->shader = shader;
		triangle->copied = TRUE;
		copied[nearby->frame] = TRUE;
		triangle_count++;
	}

	return triangle_count;
}

/* (a box's corner: across, into the wall and up, each its lower or upper
end) */
#define GEARBOX_BOX_CORNER(across, depth, height) ((across) + 2 * (depth) + 4 * (height))

/* a leaf of the collision bsp that a box's faces split, at a node's child
(the leaf, and the box) */
struct gearbox_frame_slot
{
	long node;
	short child;
	short box;
	long leaf;
};

static struct gearbox_frame_slot gearbox_frame_slots[MAXIMUM_GEARBOX_FRAME_SLOTS];

/* the box's eight corners and six faces' planes (facing out), in the world */
static void gearbox_frame_box_place(
	struct gearbox_map_frame const *frame,
	struct gearbox_frame_box const *box,
	real_point3d *corners,
	real_plane3d *planes)
{
	real c = cosine(frame->yaw), s = sine(frame->yaw);
	short across, depth, height, face;

	for (height = 0; height < 2; height++)
	{
		for (depth = 0; depth < 2; depth++)
		{
			for (across = 0; across < 2; across++)
			{
				real_point3d *corner = &corners[GEARBOX_BOX_CORNER(across, depth, height)];

				corner->x = frame->position.x - s * box->across[across] + c * box->depth[depth];
				corner->y = frame->position.y + c * box->across[across] + s * box->depth[depth];
				corner->z = frame->position.z + box->height[height];
			}
		}
	}
	/* (across: -side and side; into the wall: -forward and forward; up:
	down and up) */
	planes[0].n.i = s; planes[0].n.j = -c; planes[0].n.k = 0.0f;
	planes[1].n.i = -s; planes[1].n.j = c; planes[1].n.k = 0.0f;
	planes[2].n.i = -c; planes[2].n.j = -s; planes[2].n.k = 0.0f;
	planes[3].n.i = c; planes[3].n.j = s; planes[3].n.k = 0.0f;
	planes[4].n.i = 0.0f; planes[4].n.j = 0.0f; planes[4].n.k = -1.0f;
	planes[5].n.i = 0.0f; planes[5].n.j = 0.0f; planes[5].n.k = 1.0f;
	for (face = 0; face < 6; face++)
	{
		/* (each face's plane passes through the corner at its end) */
		real_point3d const *corner = &corners[GEARBOX_BOX_CORNER(face == 1, face == 3, face == 5)];

		planes[face].d = planes[face].n.i * corner->x + planes[face].n.j * corner->y + planes[face].n.k * corner->z;
	}

	return;
}

/* The frames' boxes (gearbox_map_frames) made solid in the Xbox map's
collision bsp: each leaf of empty space a box reaches is split by its six
faces, in a chain of nodes whose outsides are the leaf and whose last
inside is solid, and is given the faces' surfaces as references on their
planes (a hit or a touch looks a surface up in the leaf it came from). The
blocks they join are copies (nodes, planes, references, surfaces, edges,
vertices); the leaves split are changed where they were read. FALSE: the
bsp is as it was. */
static boolean gearbox_frames_collide(
	struct structure_bsp *structure_bsp,
	struct gearbox_map_frames const *frames)
{
	static real_point3d corners[MAXIMUM_GEARBOX_FRAME_BOXES][8];
	static short materials[MAXIMUM_GEARBOX_FRAMES];
	struct collision_bsp *bsp;
	long box_count = frames->count * frames->box_count;
	long node_count, node_capacity, plane_base, slot_count = 0, reference_count, index;
	long vertex_base, edge_base, surface_base;
	struct bsp3d_node *nodes;
	real_plane3d *planes;
	struct bsp2d_reference_view
	{
		long plane_designator;
		long root_index;
	} *references;
	struct collision_surface *surfaces;
	struct collision_edge *edges;
	struct collision_vertex *vertices;
	struct gearbox_collision_leaf *leaves;
	short box, frame;

	if (structure_bsp->collision_bsp.count < 1 || box_count <= 0 || box_count > MAXIMUM_GEARBOX_FRAME_BOXES)
		return FALSE;
	bsp = TAG_BLOCK_GET_ELEMENT(&structure_bsp->collision_bsp, 0, struct collision_bsp);
	leaves = (struct gearbox_collision_leaf *)bsp->leaves.address;

	/* each frame's floor's material, for its boxes' faces */
	for (frame = 0; frame < frames->count; frame++)
	{
		struct collision_bsp_test_vector_result result;
		real_point3d above = frames->frames[frame].position;
		real_vector3d down = { 0.0f, 0.0f, -1.0f };

		above.z += 0.3f;
		memset(&result, 0, sizeof(result));
		materials[frame] = collision_bsp_test_vector(FLAG(_collision_test_front_facing_surfaces_bit), bsp, 0, NULL, &above,
			&down, 1.0f, &result) ? result.material_index : 0;
	}

	node_count = bsp->bsp3d.nodes.count;
	node_capacity = node_count + 6 * MAXIMUM_GEARBOX_FRAME_SLOTS;
	plane_base = bsp->bsp3d.planes.count;
	gearbox_frames_made.collision[0] = malloc(node_capacity * sizeof(*nodes) + (plane_base + 6 * box_count) * sizeof(*planes));
	if (!gearbox_frames_made.collision[0])
		return FALSE;
	nodes = (struct bsp3d_node *)gearbox_frames_made.collision[0];
	planes = (real_plane3d *)(nodes + node_capacity);
	memcpy(nodes, bsp->bsp3d.nodes.address, node_count * sizeof(*nodes));
	memcpy(planes, bsp->bsp3d.planes.address, plane_base * sizeof(*planes));

	/* each box: its planes, the leaves it reaches (as the tree stands, the
	boxes before it in it), and a chain of its faces at each */
	for (box = 0; box < box_count; box++)
	{
		struct gearbox_map_frame const *definition = &frames->frames[box / frames->box_count];
		long stack[256];
		short stack_count = 0;
		long first_slot = slot_count, visits = 0;

		gearbox_frame_box_place(definition, &frames->boxes[box % frames->box_count], corners[box],
			&planes[plane_base + 6 * box]);
		stack[stack_count++] = 0;
		while (stack_count > 0)
		{
			long node_index = stack[--stack_count];
			struct bsp3d_node const *node;
			real_plane3d const *plane;
			boolean front = FALSE, back = FALSE;
			short corner, child;

			if (node_index < 0 || node_index >= node_count || ++visits > node_count)
				return FALSE;
			node = &nodes[node_index];
			if (node->plane_designator < 0 || node->plane_designator >= plane_base + 6 * box_count)
				return FALSE;
			plane = &planes[node->plane_designator];
			for (corner = 0; corner < 8; corner++)
			{
				if (plane3d_distance_to_point(plane, &corners[box][corner]) >= 0.0f)
					front = TRUE;
				else
					back = TRUE;
			}
			for (child = 0; child < 2; child++)
			{
				long child_index = node->children[child];

				if (!(child ? front : back))
					continue;
				if (child_index & LONG_MIN)
				{
					/* (a leaf of empty space; NONE is solid already) */
					if (child_index == NONE || (child_index & LONG_MAX) >= bsp->leaves.count)
						continue;
					if (slot_count == MAXIMUM_GEARBOX_FRAME_SLOTS)
						return FALSE;
					gearbox_frame_slots[slot_count].node = node_index;
					gearbox_frame_slots[slot_count].child = child;
					gearbox_frame_slots[slot_count].box = box;
					gearbox_frame_slots[slot_count].leaf = child_index & LONG_MAX;
					slot_count++;
				}
				else if (stack_count < (short)NUMBEROF(stack))
				{
					stack[stack_count++] = child_index;
				}
				else
				{
					return FALSE;
				}
			}
		}
		for (index = first_slot; index < slot_count; index++)
		{
			struct gearbox_frame_slot const *slot = &gearbox_frame_slots[index];
			short face;

			if (node_count + 6 > node_capacity)
				return FALSE;
			nodes[slot->node].children[slot->child] = node_count;
			for (face = 0; face < 6; face++)
			{
				nodes[node_count].plane_designator = plane_base + 6 * box + face;
				nodes[node_count].children[1] = slot->leaf | LONG_MIN;
				nodes[node_count].children[0] = face < 5 ? node_count + 1 : NONE;
				node_count++;
			}
		}
	}

	/* the references: each leaf's own, then six of each box that splits it
	(a box's faces' surfaces, on their planes, which face into the leaf) */
	reference_count = bsp->bsp2d_references.count;
	for (index = 0; index < slot_count; index++)
	{
		long other;

		for (other = 0; other < index && gearbox_frame_slots[other].leaf != gearbox_frame_slots[index].leaf; other++)
			;
		if (other == index)
			reference_count += leaves[gearbox_frame_slots[index].leaf].bsp2d_reference_count;
		for (other = 0; other < index && (gearbox_frame_slots[other].leaf != gearbox_frame_slots[index].leaf ||
			gearbox_frame_slots[other].box != gearbox_frame_slots[index].box); other++)
			;
		if (other == index)
			reference_count += 6;
	}
	vertex_base = bsp->vertices.count;
	edge_base = bsp->edges.count;
	surface_base = bsp->surfaces.count;
	gearbox_frames_made.collision[1] = malloc(reference_count * sizeof(*references) +
		(surface_base + 6 * box_count) * sizeof(*surfaces) + (edge_base + 12 * box_count) * sizeof(*edges) +
		(vertex_base + 8 * box_count) * sizeof(*vertices));
	if (!gearbox_frames_made.collision[1])
		return FALSE;
	references = (struct bsp2d_reference_view *)gearbox_frames_made.collision[1];
	surfaces = (struct collision_surface *)(references + reference_count);
	edges = (struct collision_edge *)(surfaces + surface_base + 6 * box_count);
	vertices = (struct collision_vertex *)(edges + edge_base + 12 * box_count);
	memcpy(references, bsp->bsp2d_references.address, bsp->bsp2d_references.count * sizeof(*references));
	memcpy(surfaces, bsp->surfaces.address, surface_base * sizeof(*surfaces));
	memcpy(edges, bsp->edges.address, edge_base * sizeof(*edges));
	memcpy(vertices, bsp->vertices.address, vertex_base * sizeof(*vertices));

	/* each box's surfaces: its faces, each a ring of four edges turning
	left about its outward normal, as the bsp's own are */
	for (box = 0; box < box_count; box++)
	{
		real_plane3d const *box_planes = &planes[plane_base + 6 * box];
		long box_vertices = vertex_base + 8 * box, box_edges = edge_base + 12 * box;
		short rings[6][4];
		short edge_count = 0, face, corner;

		for (corner = 0; corner < 8; corner++)
		{
			vertices[box_vertices + corner].point = corners[box][corner];
			vertices[box_vertices + corner].first_edge_index = NONE;
		}
		for (face = 0; face < 6; face++)
		{
			short axis = face / 2, end = face % 2, count = 0, k, j;
			real angles[4];
			real_vector3d const *n = &box_planes[face].n;
			real_point3d middle;
			real_vector3d u, v;
			real length;

			for (corner = 0; corner < 8; corner++)
			{
				if (((corner >> axis) & 1) == end)
					rings[face][count++] = corner;
			}
			middle.x = middle.y = middle.z = 0.0f;
			for (k = 0; k < 4; k++)
			{
				middle.x += corners[box][rings[face][k]].x / 4.0f;
				middle.y += corners[box][rings[face][k]].y / 4.0f;
				middle.z += corners[box][rings[face][k]].z / 4.0f;
			}
			u.i = corners[box][rings[face][0]].x - middle.x;
			u.j = corners[box][rings[face][0]].y - middle.y;
			u.k = corners[box][rings[face][0]].z - middle.z;
			length = square_root(u.i * u.i + u.j * u.j + u.k * u.k);
			if (length <= 0.0f)
				return FALSE;
			u.i /= length;
			u.j /= length;
			u.k /= length;
			v.i = n->j * u.k - n->k * u.j;
			v.j = n->k * u.i - n->i * u.k;
			v.k = n->i * u.j - n->j * u.i;
			for (k = 0; k < 4; k++)
			{
				real_point3d const *p = &corners[box][rings[face][k]];
				real px = p->x - middle.x, py = p->y - middle.y, pz = p->z - middle.z;

				angles[k] = arctangent(px * v.i + py * v.j + pz * v.k, px * u.i + py * u.j + pz * u.k);
			}
			/* (counterclockwise seen from outside: by angle about the normal) */
			for (k = 1; k < 4; k++)
			{
				for (j = k; j > 0 && angles[j - 1] > angles[j]; j--)
				{
					real angle = angles[j];
					short ring = rings[face][j];

					angles[j] = angles[j - 1];
					rings[face][j] = rings[face][j - 1];
					angles[j - 1] = angle;
					rings[face][j - 1] = ring;
				}
			}
		}
		/* the edges: each made by the first face that goes along it, the
		other going back along it */
		for (face = 0; face < 6; face++)
		{
			for (corner = 0; corner < 4; corner++)
			{
				long a = box_vertices + rings[face][corner], b = box_vertices + rings[face][(corner + 1) % 4];
				short edge;

				for (edge = 0; edge < edge_count; edge++)
				{
					if (edges[box_edges + edge].vertex_indices[0] == b && edges[box_edges + edge].vertex_indices[1] == a)
						break;
				}
				if (edge < edge_count)
				{
					edges[box_edges + edge].surface_indices[1] = surface_base + 6 * box + face;
					continue;
				}
				if (edge_count == 12)
					return FALSE;
				edges[box_edges + edge_count].vertex_indices[0] = a;
				edges[box_edges + edge_count].vertex_indices[1] = b;
				edges[box_edges + edge_count].surface_indices[0] = surface_base + 6 * box + face;
				edges[box_edges + edge_count].surface_indices[1] = NONE;
				edges[box_edges + edge_count].edge_indices[0] = edges[box_edges + edge_count].edge_indices[1] = NONE;
				vertices[a].first_edge_index = box_edges + edge_count;
				vertices[b].first_edge_index = box_edges + edge_count;
				edge_count++;
			}
		}
		if (edge_count != 12)
			return FALSE;
		/* each face's ring: from each edge to the next along it */
		for (face = 0; face < 6; face++)
		{
			long ring_edges[4];
			short sides[4];

			for (corner = 0; corner < 4; corner++)
			{
				long a = box_vertices + rings[face][corner], b = box_vertices + rings[face][(corner + 1) % 4];
				short edge;

				for (edge = 0; edge < 12; edge++)
				{
					struct collision_edge const *e = &edges[box_edges + edge];

					if ((e->vertex_indices[0] == a && e->vertex_indices[1] == b) ||
						(e->vertex_indices[0] == b && e->vertex_indices[1] == a))
					{
						break;
					}
				}
				if (edge == 12)
					return FALSE;
				ring_edges[corner] = box_edges + edge;
				sides[corner] = edges[box_edges + edge].vertex_indices[0] == a ? 0 : 1;
			}
			for (corner = 0; corner < 4; corner++)
				edges[ring_edges[corner]].edge_indices[sides[corner]] = ring_edges[(corner + 1) % 4];
			surfaces[surface_base + 6 * box + face].plane_designator = plane_base + 6 * box + face;
			surfaces[surface_base + 6 * box + face].first_edge_index = ring_edges[0];
			surfaces[surface_base + 6 * box + face].flags = 0;
			surfaces[surface_base + 6 * box + face].breakable_surface_index = 0;
			surfaces[surface_base + 6 * box + face].material_index = materials[box / frames->box_count];
		}
	}

	/* the leaves split: their references copied after the bsp's, with the
	boxes' (none of the bsp changes before this: nothing can fail after) */
	{
		long written = bsp->bsp2d_references.count;

		for (index = 0; index < slot_count; index++)
		{
			long leaf_index = gearbox_frame_slots[index].leaf;
			struct gearbox_collision_leaf *leaf = &leaves[leaf_index];
			long other, first = written;

			for (other = 0; other < index && gearbox_frame_slots[other].leaf != leaf_index; other++)
				;
			if (other < index)
				continue;
			if (leaf->bsp2d_reference_count > 0)
			{
				memcpy(&references[written], &references[leaf->first_bsp2d_reference_index],
					leaf->bsp2d_reference_count * sizeof(*references));
				written += leaf->bsp2d_reference_count;
			}
			for (other = index; other < slot_count; other++)
			{
				long seen;
				short face;

				if (gearbox_frame_slots[other].leaf != leaf_index)
					continue;
				for (seen = index; seen < other && (gearbox_frame_slots[seen].leaf != leaf_index ||
					gearbox_frame_slots[seen].box != gearbox_frame_slots[other].box); seen++)
					;
				if (seen < other)
					continue;
				for (face = 0; face < 6; face++)
				{
					references[written].plane_designator = plane_base + 6 * gearbox_frame_slots[other].box + face;
					references[written].root_index = (surface_base + 6 * gearbox_frame_slots[other].box + face) | LONG_MIN;
					written++;
				}
			}
			leaf->first_bsp2d_reference_index = first;
			leaf->bsp2d_reference_count = (short)(written - first);
		}
	}
	bsp->bsp3d.nodes.count = node_count;
	bsp->bsp3d.nodes.address = nodes;
	bsp->bsp3d.planes.count = plane_base + 6 * box_count;
	bsp->bsp3d.planes.address = planes;
	bsp->bsp2d_references.count = reference_count;
	bsp->bsp2d_references.address = references;
	bsp->surfaces.count = surface_base + 6 * box_count;
	bsp->surfaces.address = surfaces;
	bsp->edges.count = edge_base + 12 * box_count;
	bsp->edges.address = edges;
	bsp->vertices.count = vertex_base + 8 * box_count;
	bsp->vertices.address = vertices;
	error(_error_silent, "gearbox items: the teleporters' frames made solid (%ld boxes splitting %ld leaves)", box_count,
		slot_count);

	return TRUE;
}

/* Halo PC's teleporter frames of the map (gearbox_map_frames) drawn on its
own structure bsp, just loaded: its blocks they join are copies with them
(its surfaces, lightmaps, and the frames' clusters' subclusters), and what
the bsp had is kept where it was read (it is read again with the map) */
static void gearbox_frames_build(
	struct structure_bsp *structure_bsp)
{
	struct gearbox_map_frames const *frames = gearbox_items_globals.frames;
	long vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_environment_compressed);
	long lightmap_vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_environment_lightmap_compressed);
	struct
	{
		short frame;
		short shader;
		long triangle_count;
		struct structure_material const *template_material;
		struct structure_lightmap const *lightmap;
		boolean lit;
		/* (its triangles the Xbox frame's copies, not Halo PC's) */
		boolean copies;
	} groups[MAXIMUM_GEARBOX_FRAME_GROUPS];
	long shader_handles[MAXIMUM_GEARBOX_FRAME_GROUPS];
	boolean copied[MAXIMUM_GEARBOX_FRAMES];
	short room_lightmaps[MAXIMUM_GEARBOX_FRAMES];
	short lit_lightmaps[MAXIMUM_GEARBOX_FRAMES];
	long walls[MAXIMUM_GEARBOX_FRAMES];
	short clusters[MAXIMUM_GEARBOX_FRAMES];
	long frame_triangles[MAXIMUM_GEARBOX_FRAMES];
	long nearby_count, triangle_count, drawn_count = 0, vertices_size = 0, blocks_size, surface_count, index;
	short group_count = 0, frame, shader, group;
	byte *cursor;
	word *surfaces;
	struct structure_lightmap *lightmaps;
	long *surface_indices;
	char const *failure = NULL;

	gearbox_frames_release();
	if (!frames || !gearbox_barriers.loaded || frames->count > MAXIMUM_GEARBOX_FRAMES ||
		frames->shader_count > MAXIMUM_GEARBOX_FRAME_GROUPS)
	{
		return;
	}
	for (shader = 0; shader < frames->shader_count; shader++)
		shader_handles[shader] = gearbox_shader_handle(frames->shaders[shader]);
	nearby_count = gearbox_frames_gather_nearby(structure_bsp, frames);
	triangle_count = gearbox_frames_pick(frames, nearby_count);

	/* each frame's room: the cluster it stands in, the lightmap of the frame
	it is lit as (of its surface nearest a little above its floor), and the
	wall behind it (the room's lit upright surface nearest a little above
	its floor), whose lightmap lights it if that frame's cannot */
	for (frame = 0; frame < frames->count; frame++)
	{
		real_point3d above = frames->frames[frame].position;
		real_point3d lit_above = frames->frames[frame].lit_position;
		real nearest = -1.0f, lit_nearest = -1.0f;

		above.z += 0.5f;
		lit_above.z += 0.5f;
		lit_lightmaps[frame] = NONE;
		for (index = 0; index < nearby_count; index++)
		{
			struct gearbox_frame_nearby const *nearby = &gearbox_frames_nearby[index];
			real weights[3];
			real distance;

			if (nearby->frame != frame || !nearby->lit_source || nearby->lightmap == NONE)
				continue;
			distance = gearbox_triangle_nearest(&lit_above, nearby->corners, weights);
			if (lit_nearest < 0.0f || distance < lit_nearest)
			{
				lit_nearest = distance;
				lit_lightmaps[frame] = nearby->lightmap;
			}
		}
		clusters[frame] = (short)cluster_index_from_point(structure_bsp, &above);
		room_lightmaps[frame] = NONE;
		walls[frame] = NONE;
		frame_triangles[frame] = 0;
		for (index = 0; index < nearby_count; index++)
		{
			struct gearbox_frame_nearby const *nearby = &gearbox_frames_nearby[index];
			real_point3d const *c = nearby->corners;
			real nx = (c[1].y - c[0].y) * (c[2].z - c[0].z) - (c[1].z - c[0].z) * (c[2].y - c[0].y);
			real ny = (c[1].z - c[0].z) * (c[2].x - c[0].x) - (c[1].x - c[0].x) * (c[2].z - c[0].z);
			real nz = (c[1].x - c[0].x) * (c[2].y - c[0].y) - (c[1].y - c[0].y) * (c[2].x - c[0].x);
			real weights[3];
			real distance;

			/* (upright: its normal less than a third up or down) */
			if (nearby->frame != frame || nearby->lit_source || nearby->lightmap == NONE ||
				9.0f * nz * nz > nx * nx + ny * ny + nz * nz)
				continue;
			distance = gearbox_triangle_nearest(&above, nearby->corners, weights);
			if (nearest < 0.0f || distance < nearest)
			{
				nearest = distance;
				walls[frame] = index;
				room_lightmaps[frame] = nearby->lightmap;
			}
		}
	}

	/* the Xbox frames' lit triangles copied onto the frames */
	triangle_count = gearbox_frames_copy(frames, lit_lightmaps, shader_handles, triangle_count, nearby_count, copied);

	/* a material for each frame's triangles of each shader: a copy of the
	Xbox map's of the shader (in the lightmap it is lit from, if it has one
	there), in a lightmap of its own (that one, or that material's); a lit
	one's triangles the Xbox frame's copies where there are any */
	for (frame = 0; frame < frames->count; frame++)
	{
		for (shader = 0; shader < frames->shader_count; shader++)
		{
			long count = 0, handle = shader_handles[shader];
			short frame_lightmap = lit_lightmaps[frame] != NONE ? lit_lightmaps[frame] : room_lightmaps[frame];
			boolean copies;
			struct structure_material const *template_material = NULL;
			struct structure_lightmap const *template_lightmap = NULL;
			long lightmap_index;

			if (handle == NONE || clusters[frame] == NONE)
				continue;
			for (lightmap_index = 0; lightmap_index < structure_bsp->lightmaps.count; lightmap_index++)
			{
				struct structure_lightmap const *lightmap = TAG_BLOCK_GET_ELEMENT(&structure_bsp->lightmaps, lightmap_index,
					struct structure_lightmap);
				long material_index;

				for (material_index = 0; material_index < lightmap->materials.count; material_index++)
				{
					struct structure_material const *material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, material_index,
						struct structure_material);

					if (material->shader.index == handle &&
						(!template_material || lightmap_index == frame_lightmap))
					{
						template_material = material;
						template_lightmap = lightmap;
					}
				}
			}
			if (!template_material || group_count == MAXIMUM_GEARBOX_FRAME_GROUPS)
				continue;
			copies = template_material->lightmap_vertices.count > 0 && copied[frame];
			for (index = 0; index < triangle_count; index++)
			{
				count += gearbox_frame_triangles[index].frame == frame && gearbox_frame_triangles[index].shader == shader &&
					gearbox_frame_triangles[index].copied == copies;
			}
			if (!count)
				continue;
			groups[group_count].copies = copies;
			groups[group_count].frame = frame;
			groups[group_count].shader = shader;
			groups[group_count].triangle_count = count;
			groups[group_count].template_material = template_material;
			groups[group_count].lit = template_material->lightmap_vertices.count > 0;
			if (groups[group_count].lit && frame_lightmap == NONE)
				continue;
			groups[group_count].lightmap = groups[group_count].lit ?
				TAG_BLOCK_GET_ELEMENT(&structure_bsp->lightmaps, frame_lightmap, struct structure_lightmap) :
				template_lightmap;
			vertices_size += 3 * count * (vertex_size + (groups[group_count].lit ? lightmap_vertex_size : 0));
			drawn_count += count;
			frame_triangles[frame] += count;
			group_count++;
		}
	}
	if (!group_count)
	{
		error(_error_silent, "gearbox items: the teleporters' frames are not drawn: none of Halo PC's were found (%ld triangles)",
			triangle_count);
		return;
	}

	/* the bsp's blocks with them, the made materials, and the frames'
	subclusters with each cluster's own */
	surface_count = structure_bsp->surfaces.count + drawn_count;
	blocks_size = ((surface_count * 3 * (long)sizeof(word) + 3) & ~3) +
		(structure_bsp->lightmaps.count + group_count) * (long)sizeof(struct structure_lightmap) +
		group_count * (long)sizeof(struct structure_material) +
		drawn_count * (long)sizeof(long);
	for (frame = 0; frame < frames->count; frame++)
	{
		if (frame_triangles[frame])
		{
			struct gearbox_cluster_view const *cluster = TAG_BLOCK_GET_ELEMENT(&structure_bsp->clusters, clusters[frame],
				struct gearbox_cluster_view);

			blocks_size += (cluster->subclusters.count + frames->count) * (long)sizeof(struct gearbox_subcluster);
		}
	}
	if (surface_count > MAXIMUM_SURFACES_PER_STRUCTURE)
		failure = "the structure bsp has too many surfaces for them";
	gearbox_frames_made.blocks = failure ? NULL : malloc(blocks_size);
	gearbox_frames_made.vertices = gearbox_frames_made.blocks ?
		XPhysicalAlloc(vertices_size, (unsigned long)-1, 0, PAGE_READWRITE) : NULL;
	gearbox_frames_made.vertices_contiguous = gearbox_frames_made.vertices != NULL;
	if (gearbox_frames_made.blocks && !gearbox_frames_made.vertices)
		gearbox_frames_made.vertices = malloc(vertices_size);
	if (!failure && (!gearbox_frames_made.blocks || !gearbox_frames_made.vertices))
		failure = "out of memory";
	if (failure)
	{
		error(_error_silent, "gearbox items: the teleporters' frames are not drawn: %s", failure);
		gearbox_frames_release();
		return;
	}
	cursor = gearbox_frames_made.blocks;
	surfaces = (word *)cursor;
	cursor += (surface_count * 3 * sizeof(word) + 3) & ~3;
	lightmaps = (struct structure_lightmap *)cursor;
	cursor += (structure_bsp->lightmaps.count + group_count) * sizeof(struct structure_lightmap);
	gearbox_frames_made.materials = (struct structure_material *)cursor;
	cursor += group_count * sizeof(struct structure_material);
	surface_indices = (long *)cursor;
	cursor += drawn_count * sizeof(long);
	memcpy(surfaces, structure_bsp->surfaces.address, structure_bsp->surfaces.count * 3 * sizeof(word));
	memcpy(lightmaps, structure_bsp->lightmaps.address, structure_bsp->lightmaps.count * sizeof(struct structure_lightmap));

	/* each material: its vertices (Halo PC's, lit from the room's lightmap),
	compressed, with buffers, and its triangles after the bsp's */
	{
		byte *compressed = gearbox_frames_made.vertices;
		long surface_index = structure_bsp->surfaces.count;
		long indices_written = 0;

		for (group = 0; group < group_count; group++)
		{
			struct structure_material *material = &gearbox_frames_made.materials[group];
			struct structure_lightmap *lightmap = &lightmaps[structure_bsp->lightmaps.count + group];
			long vertex_count = 0;
			real_point3d centroid = { 0.0f, 0.0f, 0.0f };

			for (index = 0; index < triangle_count; index++)
			{
				struct gearbox_frame_triangle const *triangle = &gearbox_frame_triangles[index];
				real_point3d const *p = &triangle->vertices[0].position;
				real_point3d const *q = &triangle->vertices[1].position;
				real_point3d const *r = &triangle->vertices[2].position;
				real_vector3d normal;
				short corner;

				if (triangle->frame != groups[group].frame || triangle->shader != groups[group].shader ||
					triangle->copied != groups[group].copies)
				{
					continue;
				}
				normal.i = (q->y - p->y) * (r->z - p->z) - (q->z - p->z) * (r->y - p->y);
				normal.j = (q->z - p->z) * (r->x - p->x) - (q->x - p->x) * (r->z - p->z);
				normal.k = (q->x - p->x) * (r->y - p->y) - (q->y - p->y) * (r->x - p->x);
				for (corner = 0; corner < 3; corner++)
				{
					struct gearbox_lightmap_vertex *lightmap_vertex = &gearbox_frame_lightmap_vertices[vertex_count];

					gearbox_frame_vertices[vertex_count] = triangle->vertices[corner];
					lightmap_vertex->incident_radiosity = triangle->incident[corner];
					if (!groups[group].lit)
						lightmap_vertex->texcoord = triangle->vertices[corner].texcoord;
					else if (triangle->copied)
						lightmap_vertex->texcoord = triangle->lightmap_texcoords[corner];
					else if (lit_lightmaps[triangle->frame] != NONE)
						lightmap_vertex->texcoord = gearbox_frame_lit_texcoord(&frames->frames[triangle->frame], triangle->frame,
							lit_lightmaps[triangle->frame], nearby_count, &triangle->vertices[corner].position, &normal);
					else
						lightmap_vertex->texcoord = gearbox_frame_lightmap_texcoord(&triangle->vertices[corner].position,
							&gearbox_frames_nearby[walls[triangle->frame]]);
					centroid.x += triangle->vertices[corner].position.x;
					centroid.y += triangle->vertices[corner].position.y;
					centroid.z += triangle->vertices[corner].position.z;
					vertex_count++;
				}
				surfaces[3 * surface_index] = (word)(vertex_count - 3);
				surfaces[3 * surface_index + 1] = (word)(vertex_count - 2);
				surfaces[3 * surface_index + 2] = (word)(vertex_count - 1);
				surface_index++;
			}
			*material = *groups[group].template_material;
			material->first_surface_index = surface_index - vertex_count / 3;
			material->surface_count = vertex_count / 3;
			material->centroid.x = centroid.x / vertex_count;
			material->centroid.y = centroid.y / vertex_count;
			material->centroid.z = centroid.z / vertex_count;
			material->breakable_surface_index = NONE;
			memset(&material->vertices, 0, sizeof(material->vertices));
			memset(&material->lightmap_vertices, 0, sizeof(material->lightmap_vertices));
			memset(&material->uncompressed_vertex_data, 0, sizeof(material->uncompressed_vertex_data));
			rasterizer_geometry_compress_vertices(_rasterizer_vertex_type_environment_uncompressed, vertex_count,
				compressed, vertex_count * vertex_size, gearbox_frame_vertices,
				vertex_count * (long)sizeof(struct gearbox_environment_vertex));
			if (groups[group].lit)
			{
				rasterizer_geometry_compress_vertices(_rasterizer_vertex_type_environment_lightmap_uncompressed, vertex_count,
					compressed + vertex_count * vertex_size, vertex_count * lightmap_vertex_size, gearbox_frame_lightmap_vertices,
					vertex_count * (long)sizeof(struct gearbox_lightmap_vertex));
			}
			material->compressed_vertex_data.size = vertex_count * (vertex_size + (groups[group].lit ? lightmap_vertex_size : 0));
			material->compressed_vertex_data.address = XBOX_ADDRESS(compressed);
			gearbox_frames_made.material_count = group + 1;
			if (!rasterizer_vertex_buffer_new(&material->vertices, _rasterizer_vertex_type_environment_compressed, vertex_count,
					compressed, vertex_count * vertex_size) ||
				(groups[group].lit && !rasterizer_vertex_buffer_new(&material->lightmap_vertices,
					_rasterizer_vertex_type_environment_lightmap_compressed, vertex_count, compressed + vertex_count * vertex_size,
					vertex_count * lightmap_vertex_size)))
			{
				error(_error_silent, "gearbox items: the teleporters' frames are not drawn: their vertex buffers cannot be made");
				gearbox_frames_release();
				return;
			}
			compressed += material->compressed_vertex_data.size;
			*lightmap = *groups[group].lightmap;
			lightmap->materials.count = 1;
			lightmap->materials.address = material;
		}

		/* and each frame's triangles a subcluster of its room's cluster (two
		frames in one cluster each one) */
		for (frame = 0; frame < frames->count; frame++)
		{
			struct gearbox_cluster_view *cluster;
			struct gearbox_subcluster *subclusters, *subcluster;
			long first_index = indices_written;
			short corner;

			if (!frame_triangles[frame])
				continue;
			cluster = TAG_BLOCK_GET_ELEMENT(&structure_bsp->clusters, clusters[frame], struct gearbox_cluster_view);
			subclusters = (struct gearbox_subcluster *)cursor;
			cursor += (cluster->subclusters.count + 1) * sizeof(struct gearbox_subcluster);
			if (cluster->subclusters.count > 0)
				memcpy(subclusters, cluster->subclusters.address, cluster->subclusters.count * sizeof(*subclusters));
			subcluster = &subclusters[cluster->subclusters.count];
			memset(subcluster, 0, sizeof(*subcluster));
			if (cluster->subclusters.count > 0)
				subcluster->surface_indices.definition = subclusters[0].surface_indices.definition;
			subcluster->world_bounds.x0 = subcluster->world_bounds.y0 = subcluster->world_bounds.z0 = 1.0e6f;
			subcluster->world_bounds.x1 = subcluster->world_bounds.y1 = subcluster->world_bounds.z1 = -1.0e6f;
			for (group = 0; group < group_count; group++)
			{
				struct structure_material const *material = &gearbox_frames_made.materials[group];
				long triangle_index = 0;

				if (groups[group].frame != frame)
					continue;
				for (index = 0; index < triangle_count; index++)
				{
					struct gearbox_frame_triangle const *triangle = &gearbox_frame_triangles[index];

					if (triangle->frame != frame || triangle->shader != groups[group].shader ||
						triangle->copied != groups[group].copies)
					{
						continue;
					}
					surface_indices[indices_written++] = material->first_surface_index + triangle_index++;
					for (corner = 0; corner < 3; corner++)
					{
						real_point3d const *point = &triangle->vertices[corner].position;

						subcluster->world_bounds.x0 = MIN(subcluster->world_bounds.x0, point->x);
						subcluster->world_bounds.x1 = MAX(subcluster->world_bounds.x1, point->x);
						subcluster->world_bounds.y0 = MIN(subcluster->world_bounds.y0, point->y);
						subcluster->world_bounds.y1 = MAX(subcluster->world_bounds.y1, point->y);
						subcluster->world_bounds.z0 = MIN(subcluster->world_bounds.z0, point->z);
						subcluster->world_bounds.z1 = MAX(subcluster->world_bounds.z1, point->z);
					}
				}
			}
			subcluster->surface_indices.count = indices_written - first_index;
			subcluster->surface_indices.address = &surface_indices[first_index];
			cluster->subclusters.count++;
			cluster->subclusters.address = subclusters;
		}
	}
	structure_bsp->surfaces.count = surface_count;
	structure_bsp->surfaces.address = surfaces;
	structure_bsp->lightmaps.count += group_count;
	structure_bsp->lightmaps.address = lightmaps;
	error(_error_silent, "gearbox items: %ld triangles of Halo PC's teleporter frames drawn (%d materials)", drawn_count,
		group_count);
	if (frames->box_count > 0 && !gearbox_frames_collide(structure_bsp, frames))
		error(_error_silent, "gearbox items: the teleporters' frames are not solid: their boxes do not fit the collision bsp");

	return;
}

/* Halo PC's structure bsp of the map, read from its file at `path` to its own
place, its references rewritten as the donor tags' are (before the grown
table is made: the tags it names are added to it). FALSE (the Xbox map's is
kept) if it cannot be: not one bsp, not above the donor's tags, damaged. */
static boolean gearbox_map_bsp_read(
	struct gearbox_import *import,
	char const *path,
	struct scenario *donor_scenario)
{
	struct scenario_structure_bsp_reference const *reference = donor_scenario->structure_bsp_references.count == 1 ?
		gearbox_donor_element(import, &donor_scenario->structure_bsp_references, 0, sizeof(*reference)) : NULL;
	struct gearbox_structure_bsp_header *header;
	struct structure_bsp *structure_bsp;
	unsigned long base, size, lowest, top = CUSTOM_EDITION_TAG_CACHE_ADDRESS + CUSTOM_EDITION_TAG_CACHE_BYTES;
	char const *failure = NULL;
	FILE *stream;

	memset(&gearbox_map_bsp, 0, sizeof(gearbox_map_bsp));
	if (!reference)
	{
		error(_error_silent, "gearbox items: the Xbox map's structure bsp is kept: the Halo PC map has not one");
		return FALSE;
	}
	base = (unsigned long)reference->base_address;
	size = (unsigned long)reference->file_size;
	lowest = CUSTOM_EDITION_TAG_CACHE_ADDRESS + import->donor_bytes;
	if (reference->file_offset < 0 || reference->file_size < (long)sizeof(*header) ||
		size > GEARBOX_MAXIMUM_STRUCTURE_BSP_BYTES || base < lowest || base > top || size > top - base)
	{
		error(_error_silent, "gearbox items: the Xbox map's structure bsp is kept: Halo PC's (%08lx bytes at %08lx) is not above its tags",
			size, base);
		return FALSE;
	}
	stream = fopen(path, "rb");
	if (!stream || fseek(stream, reference->file_offset, SEEK_SET) || fread((void *)base, 1, size, stream) != size)
		failure = "it cannot be read";
	if (stream)
		fclose(stream);
	header = (struct gearbox_structure_bsp_header *)base;
	if (!failure && (header->signature != GEARBOX_STRUCTURE_BSP_SIGNATURE || header->vertex_buffer_count ||
		header->index_buffer_count || header->base_address < base + sizeof(*header) || header->base_address > base + size ||
		sizeof(struct structure_bsp) > base + size - header->base_address))
	{
		failure = "its header is not one";
	}
	if (!failure)
	{
		structure_bsp = (struct structure_bsp *)header->base_address;
		/* (its lightmaps go with its geometry, whatever the Xbox map's of their
		name are like) */
		import->own_index = gearbox_import_donor_index(import, structure_bsp->lightmap_group.index);
		if (!tag_validate_references_walk(GEARBOX_STRUCTURE_BSP_TAG, structure_bsp, (void const *)base, size,
			gearbox_import_visit, import))
		{
			failure = "its references cannot be followed";
		}
	}
	if (failure)
	{
		error(_error_silent, "gearbox items: the Xbox map's structure bsp is kept: Halo PC's %s ('%s')", failure, path);
		return FALSE;
	}
	gearbox_map_bsp.header = header;
	gearbox_map_bsp.size = size;
	gearbox_map_bsp.loaded = TRUE;
	error(_error_silent, "gearbox items: Halo PC's structure bsp of the map read (%08lx bytes at %08lx)", size, base);

	return TRUE;
}

/* The map's changes made to its scenario, once the grown table is in place:
its scenery, netgame equipment and teleporters' flags of those listed the
Halo PC map's. FALSE (nothing changed) if there is no room for them. */
static boolean gearbox_change_apply(
	struct gearbox_import *import,
	struct scenario *donor_scenario,
	struct scenario *scenario,
	struct gearbox_change_selection const *selection)
{
	struct gearbox_map_change const *change = selection->change;
	long palette_count = scenario->scenery_palette.count;
	long scenery_kept = 0, equipment_kept = 0, flags_kept = 0, removed = 0;
	struct scenario_object_palette_entry *palette;
	byte *scenery;
	struct scenario_netgame_equipment *equipment;
	struct scenario_netgame_flag *flags;
	long index, count;

	palette = gearbox_allocate((scenario->scenery_palette.count + selection->scenery_count) * sizeof(*palette));
	scenery = gearbox_allocate((scenario->scenery.count + selection->scenery_count) * SCENERY_PLACEMENT_BYTES);
	equipment = gearbox_allocate((scenario->netgame_equipment.count + selection->equipment_count) * sizeof(*equipment));
	flags = gearbox_allocate((scenario->netgame_flags.count + selection->flag_count) * sizeof(*flags));
	if (!palette || !scenery || !equipment || !flags || gearbox_items_globals.patch_count + 5 > MAXIMUM_GEARBOX_PATCHES)
	{
		error(_error_silent, "gearbox items: out of memory for Gearbox's changes to '%s'", change->map_name);
		return FALSE;
	}

	/* the scenery: the Xbox map's of the tags not listed, and Halo PC's,
	each of its tags given a palette entry if it has none */
	if (scenario->scenery_palette.count > 0)
		memcpy(palette, scenario->scenery_palette.address, scenario->scenery_palette.count * sizeof(*palette));
	for (index = 0; index < scenario->scenery.count; index++)
	{
		struct scenario_object_datum const *placement = (struct scenario_object_datum const *)
			((byte const *)scenario->scenery.address + index * SCENERY_PLACEMENT_BYTES);
		short entry = placement->palette_entry_index;

		if (entry >= 0 && entry < scenario->scenery_palette.count &&
			(gearbox_name_listed(change->scenery, gearbox_table_name(palette[entry].reference.index)) ||
				(gearbox_map_bsp.loaded &&
					gearbox_name_listed(change->bsp_scenery, gearbox_table_name(palette[entry].reference.index)))))
		{
			removed++;
			continue;
		}
		memcpy(scenery + scenery_kept++ * SCENERY_PLACEMENT_BYTES, placement, SCENERY_PLACEMENT_BYTES);
	}
	count = scenery_kept;
	for (index = 0; index < selection->scenery_count; index++)
	{
		struct scenario_object_datum *placement = (struct scenario_object_datum *)(scenery + count * SCENERY_PLACEMENT_BYTES);
		long tag = gearbox_change_scenery_tag(import, donor_scenario, selection->scenery[index]);
		long handle = tag != NONE ? gearbox_import_resolve(import, tag) : NONE;
		long entry;

		if (handle == NONE)
			continue;
		for (entry = 0; entry < palette_count && palette[entry].reference.index != handle; entry++)
			;
		if (entry == palette_count)
		{
			memset(&palette[entry], 0, sizeof(palette[entry]));
			palette[entry].reference.group_tag = 'scen';
			palette[entry].reference.name = (char *)custom_edition_cache_tag_name(import->donor_tags,
				import->donor_bytes, tag);
			palette[entry].reference.name_length = (long)strlen(palette[entry].reference.name);
			palette[entry].reference.index = handle;
			palette_count++;
		}
		memcpy(placement, gearbox_donor_element(import, &donor_scenario->scenery, selection->scenery[index],
			SCENERY_PLACEMENT_BYTES), SCENERY_PLACEMENT_BYTES);
		placement->palette_entry_index = (short)entry;
		/* (the Halo PC map's object names are not the Xbox map's) */
		placement->name_index = NONE;
		count++;
	}
	gearbox_block_replace(&scenario->scenery_palette, palette_count, palette);
	gearbox_block_replace(&scenario->scenery, count, scenery);

	/* the netgame equipment: the Xbox map's of the item collections not
	listed (the items' among them), and Halo PC's */
	for (index = 0; index < scenario->netgame_equipment.count; index++)
	{
		struct scenario_netgame_equipment const *placement =
			(struct scenario_netgame_equipment const *)scenario->netgame_equipment.address + index;

		if (!gearbox_name_listed(change->equipment, gearbox_table_name(placement->item_collection.index)))
			equipment[equipment_kept++] = *placement;
	}
	count = equipment_kept;
	for (index = 0; index < selection->equipment_count; index++)
	{
		struct scenario_netgame_equipment const *source = gearbox_donor_element(import,
			&donor_scenario->netgame_equipment, selection->equipment[index], sizeof(*source));
		long collection = gearbox_import_donor_index(import, source->item_collection.index);

		equipment[count] = *source;
		equipment[count].run_time_spawned_item_index = NONE;
		equipment[count].item_collection.index = collection != NONE ? gearbox_import_resolve(import, collection) : NONE;
		count++;
	}
	if (change->equipment)
		gearbox_block_replace(&scenario->netgame_equipment, count, equipment);

	/* the teleporters' flags */
	if (change->teleporters)
	{
		for (index = 0; index < scenario->netgame_flags.count; index++)
		{
			struct scenario_netgame_flag const *flag = (struct scenario_netgame_flag const *)scenario->netgame_flags.address + index;

			if (flag->type != _netgame_flag_teleporter_source && flag->type != _netgame_flag_teleporter_target)
				flags[flags_kept++] = *flag;
		}
		count = flags_kept;
		for (index = 0; index < selection->flag_count; index++)
		{
			flags[count++] = *(struct scenario_netgame_flag const *)gearbox_donor_element(import,
				&donor_scenario->netgame_flags, selection->flags[index], sizeof(*flags));
		}
		gearbox_block_replace(&scenario->netgame_flags, count, flags);
	}
	if (gearbox_map_bsp.loaded && change->bsp_no_decals)
		gearbox_block_replace(&scenario->decals, 0, NULL);
	if (!gearbox_map_bsp.loaded && change->arrows)
		gearbox_change_arrows_apply(import, scenario);
	error(_error_silent,
		"gearbox items: Gearbox's changes to '%s': %ld scenery placed (%ld of the map's taken away), %ld netgame equipment (%ld of the map's kept), %ld teleporters' flags",
		change->map_name, selection->scenery_count, removed, selection->equipment_count, equipment_kept,
		selection->flag_count);

	return TRUE;
}

/* the items taken from the donor loaded into the Xbox map's tags and its
scenario `scenario`, and Gearbox's changes to the map (its file's name
`map_name`); FALSE (nothing of them taken) if they cannot be */
static boolean gearbox_import(
	struct gearbox_tag_header *donor_header,
	struct scenario *scenario,
	char const *map_name)
{
	struct gearbox_import import;
	struct scenario *donor_scenario;
	long vehicle_placements[MAXIMUM_GEARBOX_PLACEMENTS];
	short vehicle_roots[MAXIMUM_GEARBOX_PLACEMENTS];
	long equipment[MAXIMUM_GEARBOX_PLACEMENTS];
	long vehicle_count = 0, equipment_count = 0;
	long index, xbox_count;
	short root;
	boolean success = FALSE;
	static struct gearbox_change_selection selection;

	memset(&import, 0, sizeof(import));
	import.own_index = NONE;
	memset(&gearbox_map_arrows, 0, sizeof(gearbox_map_arrows));
	selection.change = gearbox_map_change_find(map_name);
	import.donor_tags = custom_edition_cache_donor_tags(&import.donor_bytes);
	import.xbox_instances = cache_files_tag_instances(&xbox_count);
	import.xbox_count = xbox_count;
	import.donor_instances = donor_header->instances;
	import.donor_count = donor_header->tag_count;
	if (!import.donor_tags || !import.xbox_instances || import.donor_count <= 0 ||
		import.donor_count > UNSIGNED_SHORT_MAX)
	{
		return FALSE;
	}
	import.states = malloc(import.donor_count);
	import.handles = malloc(import.donor_count * sizeof(long));
	import.queue = malloc(import.donor_count * sizeof(long));
	import.own_names = malloc(import.donor_count * sizeof(char const *));
	if (import.own_names)
		memset(import.own_names, 0, import.donor_count * sizeof(char const *));
	if (!import.states || !import.handles || !import.queue || !import.own_names || !gearbox_import_items_only(&import))
	{
		error(_error_silent, "gearbox items: out of memory");
		gearbox_import_dispose(&import);
		return FALSE;
	}
	memset(import.states, _donor_tag_unseen, import.donor_count);
	for (index = 0; index < import.xbox_count; index++)
	{
		long salt = (long)((unsigned long)import.xbox_instances[index].tag_index >> 16);

		if (import.xbox_instances[index].tag_index != NONE && salt > import.first_salt)
			import.first_salt = salt;
	}
	import.first_salt++;

	/* the items, and their placements (read before any reference is
	rewritten: they name the donor's tags) */
	for (root = 0; root < NUMBER_OF_GEARBOX_ROOTS; root++)
		import.roots[root] = gearbox_import_donor_tag(&import, gearbox_roots[root].group_tag, gearbox_roots[root].name);
	donor_scenario = custom_edition_cache_tag_get(import.donor_tags, import.donor_bytes,
		(unsigned long)donor_header->scenario_tag_index, GEARBOX_SCENARIO_TAG, sizeof(*donor_scenario));
	for (index = 0; donor_scenario && index < donor_scenario->vehicles.count; index++)
	{
		byte const *placement = custom_edition_cache_block_element(import.donor_tags, import.donor_bytes,
			&donor_scenario->vehicles, index, VEHICLE_PLACEMENT_BYTES);
		short placed = placement ? gearbox_import_vehicle_root(&import, donor_scenario, placement) : NONE;

		if (placed != NONE && vehicle_count < MAXIMUM_GEARBOX_PLACEMENTS)
		{
			vehicle_placements[vehicle_count] = index;
			vehicle_roots[vehicle_count++] = placed;
		}
	}
	for (index = 0; donor_scenario && index < donor_scenario->netgame_equipment.count; index++)
	{
		struct scenario_netgame_equipment const *placement = custom_edition_cache_block_element(import.donor_tags,
			import.donor_bytes, &donor_scenario->netgame_equipment, index, sizeof(*placement));

		if (placement && equipment_count < MAXIMUM_GEARBOX_PLACEMENTS &&
			gearbox_import_collection_has_root(&import, gearbox_import_donor_index(&import,
				placement->item_collection.index)))
		{
			equipment[equipment_count++] = index;
		}
	}

	if (selection.change && donor_scenario && !gearbox_change_select(&import, donor_scenario, &selection))
	{
		error(_error_silent, "gearbox items: Gearbox's changes to '%s' have more placements than room for",
			selection.change->map_name);
		selection.change = NULL;
	}

	/* their tags, and those they name; and Halo PC's HUD messaging icons, of
	which the fuel rod gun's is one (gearbox_items_messaging_icons) */
	for (root = 0; root < NUMBER_OF_GEARBOX_ROOTS; root++)
	{
		if (import.roots[root] != NONE)
			gearbox_import_resolve(&import, import.roots[root]);
	}
	import.messaging_icons = NONE;
	for (index = 0; index < import.donor_count && import.messaging_icons == NONE; index++)
	{
		struct hud_globals_definition const *hud_globals = custom_edition_cache_tag_get(import.donor_tags,
			import.donor_bytes, (unsigned long)import.donor_instances[index].tag_index, GEARBOX_HUD_GLOBALS_TAG,
			sizeof(*hud_globals));

		if (hud_globals)
			import.messaging_icons = gearbox_import_donor_index(&import, hud_globals->messaging.messaging_icons.index);
	}
	if (import.messaging_icons != NONE)
		gearbox_import_resolve(&import, import.messaging_icons);
	for (index = 0; index < equipment_count; index++)
	{
		struct scenario_netgame_equipment const *placement = custom_edition_cache_block_element(import.donor_tags,
			import.donor_bytes, &donor_scenario->netgame_equipment, equipment[index], sizeof(*placement));

		gearbox_import_resolve(&import, gearbox_import_donor_index(&import, placement->item_collection.index));
	}
	if (selection.change)
	{
		char path[MAP_PATH_SIZE];

		gearbox_change_resolve(&import, donor_scenario, &selection);
		if (selection.change->structure_bsp && gearbox_items_donor_path(map_name, path))
			gearbox_map_bsp_read(&import, path, donor_scenario);
		if (!gearbox_map_bsp.loaded && selection.change->arrows)
			gearbox_change_arrows_make(&import, selection.change->arrows);
	}
	gearbox_import_rewrite(&import);
	if (!import.failed && gearbox_import_table(&import))
	{
		if (gearbox_import_placements(&import, donor_scenario, scenario, vehicle_placements, vehicle_roots,
			vehicle_count, equipment, equipment_count))
		{
			long added = 0, replaced = 0, own_named = 0;

			/* (without them the weapons are placed, but not held) */
			gearbox_import_grafts(&import);
			if (selection.change)
				gearbox_change_apply(&import, donor_scenario, scenario, &selection);
			gearbox_items_globals.extended_string_lists = gearbox_import_string_lists(&import);
			gearbox_items_globals.has_flying_ceiling = selection.change && selection.change->has_flying_ceiling;
			gearbox_items_globals.flying_ceiling = gearbox_items_globals.has_flying_ceiling ?
				selection.change->flying_ceiling : 0.0f;
			gearbox_items_globals.frames = selection.change ? selection.change->frames : NULL;
			for (index = 0; index < import.donor_count; index++)
			{
				added += import.states[index] == _donor_tag_added;
				replaced += import.states[index] == _donor_tag_replaces;
				own_named += import.own_names[index] != NULL;
			}
			for (root = 0; root < NUMBER_OF_GEARBOX_ROOTS; root++)
			{
				gearbox_items_globals.roots[root] = import.roots[root] != NONE &&
					import.states[import.roots[root]] != _donor_tag_unseen ? import.handles[import.roots[root]] : NONE;
			}
			gearbox_items_globals.messaging_icons = import.messaging_icons != NONE &&
				import.states[import.messaging_icons] != _donor_tag_unseen ? import.handles[import.messaging_icons] : NONE;
			error(_error_silent,
				"gearbox items: %ld tags added (%ld Halo PC bitmaps beside the map's of their names) and %ld of the map's replaced by Halo PC's; %ld vehicles and %ld netgame equipment placed; %ld weapon classes added to its animations; %ld HUD item message lists extended",
				added, own_named, replaced, vehicle_count, equipment_count, gearbox_items_globals.grafted_classes,
				gearbox_items_globals.extended_string_lists);
			success = TRUE;
		}
		else
		{
			cache_files_set_tag_instances(gearbox_items_globals.original_instances,
				gearbox_items_globals.original_count);
			free(gearbox_items_globals.instances);
			free(gearbox_items_globals.donor_handles);
			gearbox_items_globals.instances = NULL;
			gearbox_items_globals.donor_handles = NULL;
		}
	}
	gearbox_import_dispose(&import);

	return success;
}

/* ---------- public code */

unsigned long gearbox_items_map_version(
	char const *map_name)
{
	struct cache_file_identity identity;
	char path[MAP_PATH_SIZE];
	char const *name;
	unsigned long version;

	if (!map_name || !map_name[0] || !config_boolean("game.gearbox_items") || custom_edition_level_name(map_name) ||
		mcc_level_name(map_name))
	{
		return 0;
	}
	name = tag_name_strip_path(map_name);
	if (!gearbox_items_donor_path(name, path) || !custom_edition_cache_file_identify(path, &identity) ||
		identity.scenario_type != _scenario_type_multiplayer || csstrcasecmp(identity.name, name))
	{
		return 0;
	}
	version = (unsigned long)identity.checksum ^ GEARBOX_ITEMS_FORMAT;
	/* (and with Gearbox's changes to the map, another) */
	if (gearbox_map_change_find(name))
		version ^= GEARBOX_MAP_CHANGES_FORMAT;

	return version ? version : 1;
}

void gearbox_items_tags_loaded(
	char const *map_name,
	long scenario_index)
{
	struct network_game *game = network_game_get_game();
	struct gearbox_tag_header *donor;
	struct scenario *scenario;
	char path[MAP_PATH_SIZE];
	unsigned long version;
	short root;

	if (gearbox_items_globals.loaded)
		return;
	for (root = 0; root < NUMBER_OF_GEARBOX_ROOTS; root++)
		gearbox_items_globals.roots[root] = NONE;
	if (custom_edition_cache_tags_loaded() || mcc_cache_tags_loaded() ||
		!tag_index_is_group(scenario_index, GEARBOX_SCENARIO_TAG))
	{
		return;
	}
	scenario = tag_get(GEARBOX_SCENARIO_TAG, scenario_index);
	if (scenario->type != _scenario_type_multiplayer)
		return;
	version = gearbox_items_map_version(map_name);
	/* (why not, when they are on: the Halo PC folder not set, or without
	this map) */
	if (!version)
	{
		if (config_boolean("game.gearbox_items"))
		{
			error(_error_silent, "gearbox items: not added to '%s': %s", map_name,
				!custom_edition_install_present() ? "paths.custom_edition does not name the Halo PC folder" :
				!gearbox_items_donor_path(map_name, path) ? "its name is too long" :
				"the Halo PC folder's maps folder has no multiplayer map of its name");
		}
		return;
	}
	/* (a network game's machines all have them, or none does: the host's
	version is the game's) */
	if (game && game->map.name[0] && !csstrcasecmp(tag_name_strip_path(game->map.name), map_name) &&
		(unsigned long)game->map.version != version)
	{
		error(_error_silent, "gearbox items: not added to '%s': the host's game is without them", map_name);
		return;
	}
	if (!gearbox_items_donor_path(map_name, path))
		return;
	donor = (struct gearbox_tag_header *)custom_edition_cache_donor_load(path);
	if (!donor)
	{
		error(_error_silent, "gearbox items: not added to '%s': '%s' cannot be loaded", map_name, path);
		return;
	}
	if (!gearbox_import(donor, scenario, map_name))
	{
		error(_error_silent, "gearbox items: not added to '%s'", map_name);
		gearbox_patches_undo();
		custom_edition_cache_donor_unload();
		return;
	}
	gearbox_items_globals.loaded = TRUE;
	memset(gearbox_flyers, 0xFF, sizeof(gearbox_flyers));
	gearbox_next_flyer = 0;
	gearbox_barriers_load(path, donor);

	return;
}

void gearbox_items_tags_unloaded(
	void)
{
	struct scenario *scenario = gearbox_items_globals.scenario;
	short root;

	if (!gearbox_items_globals.loaded)
		return;
	gearbox_patches_undo();
	if (scenario)
	{
		scenario->vehicles = gearbox_items_globals.vehicles;
		scenario->vehicle_palette = gearbox_items_globals.vehicle_palette;
		scenario->netgame_equipment = gearbox_items_globals.netgame_equipment;
	}
	if (gearbox_items_globals.added_vehicles)
		free(gearbox_items_globals.added_vehicles);
	if (gearbox_items_globals.added_vehicle_palette)
		free(gearbox_items_globals.added_vehicle_palette);
	if (gearbox_items_globals.added_netgame_equipment)
		free(gearbox_items_globals.added_netgame_equipment);
	cache_files_set_tag_instances(gearbox_items_globals.original_instances, gearbox_items_globals.original_count);
	free(gearbox_items_globals.instances);
	free(gearbox_items_globals.donor_handles);
	custom_edition_cache_donor_unload();
	/* (Halo PC's structure bsp was in the donor's tag cache) */
	memset(&gearbox_map_bsp, 0, sizeof(gearbox_map_bsp));
	memset(&gearbox_map_arrows, 0, sizeof(gearbox_map_arrows));
	gearbox_frames_release();
	if (gearbox_barriers.data)
		free(gearbox_barriers.data);
	memset(&gearbox_barriers, 0, sizeof(gearbox_barriers));
	memset(&gearbox_items_globals, 0, sizeof(gearbox_items_globals));
	for (root = 0; root < NUMBER_OF_GEARBOX_ROOTS; root++)
		gearbox_items_globals.roots[root] = NONE;

	return;
}

boolean gearbox_items_loaded(
	void)
{
	return gearbox_items_globals.loaded;
}

void *gearbox_items_structure_bsp(
	unsigned long *size)
{
	if (!gearbox_items_globals.loaded || !gearbox_map_bsp.loaded)
		return NULL;
	if (size)
		*size = gearbox_map_bsp.size;
	gearbox_map_bsp.in_use = TRUE;

	return gearbox_map_bsp.header;
}

boolean gearbox_items_structure_bsp_release(
	void)
{
	boolean in_use = gearbox_map_bsp.in_use;

	gearbox_map_bsp.in_use = FALSE;

	return in_use;
}

void gearbox_items_structure_bsp_loaded(
	struct structure_bsp *structure_bsp)
{
	struct gearbox_map_arrows const *arrows = gearbox_map_arrows.arrows;
	struct structure_runtime_decal *decals = gearbox_map_arrows.runtime_decals;
	short clusters[MAXIMUM_GEARBOX_ARROWS];
	long cluster_index, count = 0, placed = 0;
	short arrow_index;

	if (!gearbox_items_globals.loaded || !structure_bsp)
		return;
	/* (the bsp's own ceiling, which pushes a flyer down, a little below the
	map's flying ceiling, at which gearbox_items_update holds it) */
	if (gearbox_items_globals.has_flying_ceiling)
		structure_bsp->vehicle_ceiling = gearbox_items_globals.flying_ceiling - GEARBOX_SOFT_CEILING_DEPTH;
	/* (Halo PC's teleporter frames on the map's own bsp: Halo PC's has them) */
	if (!gearbox_map_bsp.in_use)
		gearbox_frames_build(structure_bsp);
	if (!arrows)
		return;
	if (structure_bsp->runtime_decals.count < 0 ||
		structure_bsp->runtime_decals.count + arrows->count > MAXIMUM_GEARBOX_RUNTIME_DECALS)
	{
		error(_error_silent, "gearbox items: the map's arrows are not drawn: its bsp has %ld decals",
			structure_bsp->runtime_decals.count);
		return;
	}
	/* (each in the cluster of the floor under it, where its decal is made
	and taken away) */
	for (arrow_index = 0; arrow_index < arrows->count; arrow_index++)
	{
		real_point3d point = arrows->arrows[arrow_index].position;

		point.z += 0.05f;
		clusters[arrow_index] = gearbox_map_arrows.palette_indices[arrow_index] != NONE ?
			(short)cluster_index_from_point(structure_bsp, &point) : NONE;
	}
	/* the bsp's decals, each cluster's followed by its arrows */
	for (cluster_index = 0; cluster_index < structure_bsp->clusters.count; cluster_index++)
	{
		struct structure_cluster_runtime_decals *cluster = TAG_BLOCK_GET_ELEMENT(&structure_bsp->clusters, cluster_index,
			struct structure_cluster_runtime_decals);
		long first = count;

		if (cluster->first_decal_index != NONE && cluster->decal_count &&
			cluster->first_decal_index >= 0 && cluster->first_decal_index + cluster->decal_count <= structure_bsp->runtime_decals.count)
		{
			memcpy(&decals[count], TAG_BLOCK_GET_ELEMENT(&structure_bsp->runtime_decals, cluster->first_decal_index,
				struct structure_runtime_decal), cluster->decal_count * sizeof(*decals));
			count += cluster->decal_count;
		}
		for (arrow_index = 0; arrow_index < arrows->count; arrow_index++)
		{
			struct gearbox_map_arrow const *arrow = &arrows->arrows[arrow_index];
			struct structure_runtime_decal *decal = &decals[count];

			if (clusters[arrow_index] != cluster_index)
				continue;
			/* (cast from above the floor, down and toward the arrow's right:
			a decal's texture runs across its cast's way) */
			decal->position = arrow->position;
			decal->position.z += GEARBOX_ARROW_HEIGHT;
			decal->palette_index = (byte)gearbox_map_arrows.palette_indices[arrow_index];
			decal->unused = 0;
			decal->yaw = (char)(long)floor(arctangent(-arrow->tip.i, arrow->tip.j) * (127.0f / _pi) + 0.5f);
			decal->pitch = GEARBOX_ARROW_PITCH;
			count++;
			placed++;
		}
		cluster->first_decal_index = count > first ? (short)first : NONE;
		cluster->decal_count = (word)(count - first);
	}
	structure_bsp->runtime_decals.count = count;
	structure_bsp->runtime_decals.address = decals;
	error(_error_silent, "gearbox items: %ld of the map's %d arrows drawn", placed, arrows->count);

	return;
}

long gearbox_items_weapon(
	short weapon)
{
	if (!gearbox_items_globals.loaded || weapon < 0 || weapon >= NUMBER_OF_GEARBOX_WEAPONS)
		return NONE;

	return gearbox_items_globals.roots[FIRST_GEARBOX_WEAPON_ROOT + weapon];
}

long gearbox_items_messaging_icons(
	long bitmap_group_index,
	short sequence_index)
{
	if (!gearbox_items_globals.loaded || gearbox_items_globals.messaging_icons == NONE || sequence_index < 0 ||
		gearbox_items_globals.messaging_icons == bitmap_group_index)
	{
		return bitmap_group_index;
	}
	if (bitmap_group_index != NONE && tag_index_is_group(bitmap_group_index, GEARBOX_BITMAP_TAG) &&
		sequence_index < ((struct bitmap_group *)tag_get(GEARBOX_BITMAP_TAG, bitmap_group_index))->sequences.count)
	{
		return bitmap_group_index;
	}

	return gearbox_items_globals.messaging_icons;
}

/* ---- Halo PC's barriers. Halo PC's maps of the game's own names hold a
flying vehicle in with invisible walls (two-sided invisible surfaces above
their cliffs and at their corners) and a lower ceiling, which the Xbox's maps
have not: a Banshee there could fly to where its camera left the map. The
first structure bsp of the Halo PC map the items came from is read from its
file (its tags, which say where, are the donor's: custom_edition_cache.c),
and its collision bsp's blocks, each in what was read, are rebased onto the
copy and every index the collision tests follow checked, as the tag
validator checks a map's (tag_schema_collision.c): a bsp that is not right
is not used (logged). */

/* (gearbox_barriers_load) the block at `block` of the bsp read at `base`
(`size` bytes from `data`), of elements `element_bytes` each, rebased onto
`data`: FALSE if it is not in it */
static boolean gearbox_barrier_block(
	struct tag_block *block,
	byte *data,
	unsigned long size,
	unsigned long base,
	unsigned long element_bytes)
{
	unsigned long offset = (unsigned long)block->address - base;

	if (block->count < 0 || (unsigned long)block->count > size / element_bytes)
		return FALSE;
	if (!block->count)
	{
		block->address = NULL;
		return TRUE;
	}
	if ((unsigned long)block->address < base || offset > size || (unsigned long)block->count * element_bytes > size - offset)
		return FALSE;
	block->address = data + offset;

	return TRUE;
}

/* (gearbox_barriers_load) a bsp child: a leaf or surface (LONG_MIN | its
index, below `marked`), none (when `none`), or a node (below `nodes`) */
static boolean gearbox_barrier_child(
	long child,
	long marked,
	long nodes,
	boolean none)
{
	if (child == NONE)
		return none;
	return child & LONG_MIN ? (child & LONG_MAX) < marked : child < nodes;
}

/* (gearbox_barriers_load) whether every index in the collision bsp is one of
its: the planes, nodes, leaves, bsp2d references and nodes, surfaces, edges
and vertices */
static boolean gearbox_barrier_indices_valid(
	struct collision_bsp const *bsp)
{
	long index;

	if (!bsp->bsp3d.planes.count || !bsp->bsp3d.nodes.count)
		return FALSE;
	for (index = 0; index < bsp->bsp3d.nodes.count; index++)
	{
		struct bsp3d_node const *node = (struct bsp3d_node const *)bsp->bsp3d.nodes.address + index;

		if ((node->plane_designator & LONG_MAX) >= bsp->bsp3d.planes.count ||
			!gearbox_barrier_child(node->children[0], bsp->leaves.count, bsp->bsp3d.nodes.count, TRUE) ||
			!gearbox_barrier_child(node->children[1], bsp->leaves.count, bsp->bsp3d.nodes.count, TRUE))
		{
			return FALSE;
		}
	}
	for (index = 0; index < bsp->leaves.count; index++)
	{
		struct gearbox_collision_leaf const *leaf = (struct gearbox_collision_leaf const *)bsp->leaves.address + index;

		if (leaf->bsp2d_reference_count < 0 || (leaf->bsp2d_reference_count &&
			(leaf->first_bsp2d_reference_index < 0 ||
				leaf->first_bsp2d_reference_index > bsp->bsp2d_references.count - leaf->bsp2d_reference_count)))
		{
			return FALSE;
		}
	}
	for (index = 0; index < bsp->bsp2d_references.count; index++)
	{
		struct gearbox_bsp2d_reference const *reference =
			(struct gearbox_bsp2d_reference const *)bsp->bsp2d_references.address + index;

		if ((reference->plane_designator & LONG_MAX) >= bsp->bsp3d.planes.count ||
			!gearbox_barrier_child(reference->root_index, bsp->surfaces.count, bsp->bsp2d.nodes.count, FALSE))
		{
			return FALSE;
		}
	}
	for (index = 0; index < bsp->bsp2d.nodes.count; index++)
	{
		struct bsp2d_node const *node = (struct bsp2d_node const *)bsp->bsp2d.nodes.address + index;

		if (!gearbox_barrier_child(node->child_indices[0], bsp->surfaces.count, bsp->bsp2d.nodes.count, FALSE) ||
			!gearbox_barrier_child(node->child_indices[1], bsp->surfaces.count, bsp->bsp2d.nodes.count, FALSE))
		{
			return FALSE;
		}
	}
	for (index = 0; index < bsp->surfaces.count; index++)
	{
		struct collision_surface const *surface = (struct collision_surface const *)bsp->surfaces.address + index;

		if ((surface->plane_designator & LONG_MAX) >= bsp->bsp3d.planes.count ||
			surface->first_edge_index < 0 || surface->first_edge_index >= bsp->edges.count)
		{
			return FALSE;
		}
	}
	for (index = 0; index < bsp->edges.count; index++)
	{
		struct collision_edge const *edge = (struct collision_edge const *)bsp->edges.address + index;
		short side;

		for (side = 0; side < 2; side++)
		{
			if (edge->vertex_indices[side] < 0 || edge->vertex_indices[side] >= bsp->vertices.count ||
				edge->edge_indices[side] < 0 || edge->edge_indices[side] >= bsp->edges.count ||
				(edge->surface_indices[side] != NONE &&
					(edge->surface_indices[side] < 0 || edge->surface_indices[side] >= bsp->surfaces.count)))
			{
				return FALSE;
			}
		}
	}
	for (index = 0; index < bsp->vertices.count; index++)
	{
		struct collision_vertex const *vertex = (struct collision_vertex const *)bsp->vertices.address + index;

		if (vertex->first_edge_index < 0 || vertex->first_edge_index >= bsp->edges.count)
			return FALSE;
	}

	return TRUE;
}

/* Reads Halo PC's collision of the map from its file at `path`, whose tags
are the donor's (`donor`): its first structure bsp's (gearbox_barriers) */
static void gearbox_barriers_load(
	char const *path,
	struct gearbox_tag_header const *donor)
{
	unsigned long donor_bytes;
	byte *donor_tags = custom_edition_cache_donor_tags(&donor_bytes);
	struct scenario const *donor_scenario;
	struct scenario_structure_bsp_reference const *reference;
	struct gearbox_structure_bsp_header header;
	struct structure_bsp const *structure_bsp;
	struct collision_bsp bsp;
	unsigned long base, size, offset;
	char const *failure = NULL;
	FILE *stream = NULL;
	byte *data = NULL;
	long index;

	memset(&gearbox_barriers, 0, sizeof(gearbox_barriers));
	donor_scenario = donor_tags ? custom_edition_cache_tag_get(donor_tags, donor_bytes,
		(unsigned long)donor->scenario_tag_index, GEARBOX_SCENARIO_TAG, sizeof(*donor_scenario)) : NULL;
	reference = donor_scenario ? custom_edition_cache_block_element(donor_tags, donor_bytes,
		&donor_scenario->structure_bsp_references, 0, sizeof(*reference)) : NULL;
	if (!reference)
	{
		failure = "its scenario names no structure bsp";
		goto done;
	}
	base = (unsigned long)reference->base_address;
	size = (unsigned long)reference->file_size;
	if (reference->file_offset < 0 || reference->file_size < (long)sizeof(header) ||
		size > GEARBOX_MAXIMUM_STRUCTURE_BSP_BYTES)
	{
		failure = "its structure bsp's size is not one";
		goto done;
	}
	stream = fopen(path, "rb");
	data = malloc(size);
	if (!stream || !data || fseek(stream, reference->file_offset, SEEK_SET) || fread(data, 1, size, stream) != size)
	{
		failure = "its structure bsp could not be read";
		goto done;
	}
	memcpy(&header, data, sizeof(header));
	offset = header.base_address - base;
	if (header.signature != GEARBOX_STRUCTURE_BSP_SIGNATURE || header.base_address < base || offset > size ||
		sizeof(struct structure_bsp) > size - offset)
	{
		failure = "its structure bsp's header is not one";
		goto done;
	}
	structure_bsp = (struct structure_bsp const *)(data + offset);
	offset = (unsigned long)structure_bsp->collision_bsp.address - base;
	if (structure_bsp->collision_bsp.count < 1 || (unsigned long)structure_bsp->collision_bsp.address < base ||
		offset > size || sizeof(bsp) > size - offset)
	{
		failure = "its structure bsp has no collision bsp";
		goto done;
	}
	memcpy(&bsp, data + offset, sizeof(bsp));
	if (!gearbox_barrier_block(&bsp.bsp3d.nodes, data, size, base, sizeof(struct bsp3d_node)) ||
		!gearbox_barrier_block(&bsp.bsp3d.planes, data, size, base, sizeof(real_plane3d)) ||
		!gearbox_barrier_block(&bsp.leaves, data, size, base, sizeof(struct gearbox_collision_leaf)) ||
		!gearbox_barrier_block(&bsp.bsp2d_references, data, size, base, sizeof(struct gearbox_bsp2d_reference)) ||
		!gearbox_barrier_block(&bsp.bsp2d.nodes, data, size, base, sizeof(struct bsp2d_node)) ||
		!gearbox_barrier_block(&bsp.surfaces, data, size, base, sizeof(struct collision_surface)) ||
		!gearbox_barrier_block(&bsp.edges, data, size, base, sizeof(struct collision_edge)) ||
		!gearbox_barrier_block(&bsp.vertices, data, size, base, sizeof(struct collision_vertex)) ||
		!gearbox_barrier_indices_valid(&bsp))
	{
		failure = "its collision bsp is damaged";
		goto done;
	}
	gearbox_barriers.bsp = bsp;
	gearbox_barriers.data = data;
	gearbox_barriers.loaded = TRUE;
	gearbox_barriers.base = base;
	gearbox_barriers.size = size;
	gearbox_barriers.structure_bsp = structure_bsp;
	gearbox_barriers.donor = donor;
	data = NULL;
	for (index = 0; index < bsp.surfaces.count; index++)
	{
		if (TEST_FLAG(((struct collision_surface const *)bsp.surfaces.address)[index].flags, GEARBOX_SURFACE_INVISIBLE_BIT))
			gearbox_barriers.barrier_surfaces++;
	}
	error(_error_silent, "gearbox items: Halo PC's collision of the map read (%ld surfaces, %ld of them invisible walls)",
		bsp.surfaces.count, gearbox_barriers.barrier_surfaces);

done:
	if (failure)
		error(_error_silent, "gearbox items: Halo PC's barriers are not used: %s ('%s')", failure, path);
	if (stream)
		fclose(stream);
	if (data)
		free(data);

	return;
}

/* whether a Banshee's move this tick, from `from` by `move`, crosses one of
Halo PC's barriers: an invisible wall, or a ceiling it rises into; its
plane's normal (unit length) in `normal` */
static boolean gearbox_barrier_crossed(
	real_point3d const *from,
	real_vector3d const *move,
	real_vector3d *normal)
{
	struct collision_bsp_test_vector_result result;
	real k;

	if (!gearbox_barriers.loaded || (move->i == 0.0f && move->j == 0.0f && move->k == 0.0f))
		return FALSE;
	if (!collision_bsp_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_back_facing_surfaces_bit) | FLAG(_collision_test_ignore_breakable_surfaces_bit),
		&gearbox_barriers.bsp, 0, NULL, from, move, 1.0f, &result) || !result.plane)
	{
		return FALSE;
	}
	k = result.plane->n.k < 0.0f ? -result.plane->n.k : result.plane->n.k;
	if (!TEST_FLAG(result.flags, GEARBOX_SURFACE_INVISIBLE_BIT) && !(move->k > 0.0f && k > 0.7f))
		return FALSE;
	*normal = result.plane->n;

	return TRUE;
}

/* ---- the items' Banshee kept in the map: the Xbox's maps were not made for
flying, and a Banshee could fly out past their edges, where Halo PC's
maps of the same name hold it in with Halo PC's barriers (above), and the
map's own edge: each tick a Banshee whose centre is
outside the map's structure (in no cluster), or over no ground of it (its
sky goes on past the cliffs, over nothing: a ray straight down meets no
front of its surfaces), is put back where it last was inside it, and
stopped: the map's edge is a wall to it. Every machine does
it, the host's deciding (its updates move the clients' copies). */

void gearbox_items_update(
	void)
{
	struct object_iterator iterator;
	long banshee = gearbox_items_globals.roots[_gearbox_root_banshee];

	if (!gearbox_items_globals.loaded || banshee == NONE)
		return;
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while (object_iterator_next(&iterator))
	{
		struct object_datum *object = object_get(iterator.index);
		struct location location;
		struct collision_result collision;
		long flyer;

		if (object->definition_index != banshee)
			continue;
		/* (one above the map's flying ceiling is held at it, its climb taken
		away) */
		if (gearbox_items_globals.has_flying_ceiling && object->object.position.z > gearbox_items_globals.flying_ceiling)
		{
			real_point3d held = object->object.position;

			held.z = gearbox_items_globals.flying_ceiling;
			object_set_position(iterator.index, &held, NULL, NULL);
			if (object->object.translational_velocity.k > 0.0f)
				object->object.translational_velocity.k = 0.0f;
		}
		for (flyer = 0; flyer < GEARBOX_MAXIMUM_FLYERS && gearbox_flyers[flyer].object_index != iterator.index; flyer++)
			;
		/* (one crossing Halo PC's barriers is put back where it was, and
		slides along them: its velocity into them taken away) */
		if (flyer < GEARBOX_MAXIMUM_FLYERS)
		{
			real_vector3d move;
			real_vector3d normal;

			move.i = object->object.position.x - gearbox_flyers[flyer].inside.x;
			move.j = object->object.position.y - gearbox_flyers[flyer].inside.y;
			move.k = object->object.position.z - gearbox_flyers[flyer].inside.z;
			if (gearbox_barrier_crossed(&gearbox_flyers[flyer].inside, &move, &normal))
			{
				real into = object->object.translational_velocity.i * normal.i +
					object->object.translational_velocity.j * normal.j +
					object->object.translational_velocity.k * normal.k;

				object_set_position(iterator.index, &gearbox_flyers[flyer].inside, NULL, NULL);
				object->object.translational_velocity.i -= into * normal.i;
				object->object.translational_velocity.j -= into * normal.j;
				object->object.translational_velocity.k -= into * normal.k;
				continue;
			}
		}
		scenario_location_from_point(&location, &object->object.position);
		if (location.cluster_index != NONE &&
			collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
				&object->object.position, &gearbox_down, iterator.index, &collision) &&
			collision.type == _collision_result_structure)
		{
			/* (a Banshee not seen before takes the next slot, the oldest's) */
			if (flyer == GEARBOX_MAXIMUM_FLYERS)
			{
				flyer = gearbox_next_flyer;
				gearbox_next_flyer = (gearbox_next_flyer + 1) % GEARBOX_MAXIMUM_FLYERS;
				gearbox_flyers[flyer].object_index = iterator.index;
			}
			gearbox_flyers[flyer].inside = object->object.position;
			continue;
		}
		if (flyer == GEARBOX_MAXIMUM_FLYERS)
			continue;
		object_set_position(iterator.index, &gearbox_flyers[flyer].inside, NULL, NULL);
		object->object.translational_velocity.i = 0.0f;
		object->object.translational_velocity.j = 0.0f;
		object->object.translational_velocity.k = 0.0f;
	}

	return;
}

boolean gearbox_items_read(
	long tag_index,
	long offset,
	long size,
	void *buffer)
{
	long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index);

	if (!gearbox_items_globals.loaded || tag_index == NONE || index >= gearbox_items_globals.count ||
		gearbox_items_globals.donor_handles[index] == NONE ||
		gearbox_items_globals.instances[index].tag_index != tag_index)
	{
		return FALSE;
	}
	custom_edition_cache_read(gearbox_items_globals.donor_handles[index], offset, size, buffer);

	return TRUE;
}
