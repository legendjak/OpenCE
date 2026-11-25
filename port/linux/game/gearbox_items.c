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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

/* the import's own part of a map's version: a build that imports otherwise
records another */
#define GEARBOX_ITEMS_FORMAT 0x47425831UL

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
#define GEARBOX_OWN_NAME_SUFFIX " (halo pc)"

/* a vehicle placement's bytes (tag_schema_scenario.c's unit_placement) */
#define VEHICLE_PLACEMENT_BYTES 0x78

/* the most placements taken from the Halo PC map (its maps have at most 8
of the items' vehicles and 4 of their weapons) */
#define MAXIMUM_GEARBOX_PLACEMENTS 64

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
	{ GEARBOX_WEAPON_TAG, "weapons\\flamethrower\\flamethrower" },
	{ GEARBOX_WEAPON_TAG, "weapons\\plasma_cannon\\plasma_cannon" },
};

/* the items' own folders: their tags are Halo PC's */
static char const *const gearbox_folders[] =
{
	"vehicles\\rwarthog\\",
	"vehicles\\banshee\\",
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
} gearbox_items_globals;

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
		!gearbox_bitmaps_alike(import, import->xbox_instances[map_tag].base_address,
			custom_edition_cache_tag_get(import->donor_tags, import->donor_bytes, (unsigned long)donor->tag_index,
				GEARBOX_BITMAP_TAG, sizeof(struct bitmap_group))))
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

/* the items taken from the donor loaded into the Xbox map's tags and its
scenario `scenario`; FALSE (nothing of them taken) if they cannot be */
static boolean gearbox_import(
	struct gearbox_tag_header *donor_header,
	struct scenario *scenario)
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

	memset(&import, 0, sizeof(import));
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
	gearbox_import_rewrite(&import);
	if (!import.failed && gearbox_import_table(&import))
	{
		if (gearbox_import_placements(&import, donor_scenario, scenario, vehicle_placements, vehicle_roots,
			vehicle_count, equipment, equipment_count))
		{
			long added = 0, replaced = 0, own_named = 0;

			/* (without them the weapons are placed, but not held) */
			gearbox_import_grafts(&import);
			gearbox_items_globals.extended_string_lists = gearbox_import_string_lists(&import);
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
	if (!gearbox_import(donor, scenario))
	{
		error(_error_silent, "gearbox items: not added to '%s'", map_name);
		gearbox_patches_undo();
		custom_edition_cache_donor_unload();
		return;
	}
	gearbox_items_globals.loaded = TRUE;

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
