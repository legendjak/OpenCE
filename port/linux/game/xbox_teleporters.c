/*
XBOX_TELEPORTERS.C

The Xbox's teleporter field on Halo PC's maps (custom-387). Halo PC draws the
teleporters' field with a transparent chicago extended shader
(TELEPORTER_SHADER_NAME: a green frame added over moving dust, which this
build reads as a chicago shader), where the Xbox's maps draw it with a
generic one of the same name ('sotr', a type Halo PC has not), a swirl of
energy. When a Halo PC (or Custom Edition) map with that shader loads, the
Xbox's is read from the first of the game's own multiplayer maps that has
it (inflated as the game inflates them), with its bitmaps and their
pixels, and stands in for Halo PC's: the shader's tag becomes the Xbox's,
which every reference to it then finds, the bitmaps are added to the map's
tags (after the menus' own, menu_tags.c, and taken away before them), and
their pixels are served from what was read (cache_file_read). It is read
once a run.

The Xbox map is the player's file: everything taken from it is checked to
be in what was read, the shader's blocks no longer than the game's, and its
values that index the renderer's tables in their ranges, as the tag
validator checks a map's (tag_schema_render.c); the bitmaps' fields are the
texture cache's to check, as a map's are (xbox_texture_cache.c). What
cannot be read leaves Halo PC's shader (logged once).
display.xbox_teleporters turns it off.
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "bitmaps/bitmap_group.h"
#include "cache/cache_files.h"
#include "tag_files/tag_groups.h"
#include "objects/objects.h"
#include "custom_edition_cache.h"
#include "xbox_teleporters.h"
#include "../../third_party/zlib/zlib_prefixed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

#define TELEPORTER_SHADER_NAME "scenery\\teleporter_shield\\shaders\\teleporter_shield"

enum
{
	XBOX_CACHE_HEADER_BYTES = 0x800,
	XBOX_CACHE_VERSION = 5,
	XBOX_CACHE_SIGNATURE = 'head',
	/* (the Xbox's tag data's address: cache_files.c) */
	XBOX_TAG_BASE = 0x803A6000,
	XBOX_TAG_INSTANCE_BYTES = 0x20,
	/* the most of an Xbox map inflated */
	XBOX_MAXIMUM_MAP_BYTES = 0x10000000,

	GENERIC_SHADER_BYTES = 0x6C,
	GENERIC_SHADER_TYPE = 5,
	GENERIC_MAP_BYTES = 0x64,
	GENERIC_STAGE_BYTES = 0x70,
	/* (tag_schema_render.c's) */
	MAXIMUM_GENERIC_MAPS = 4,
	MAXIMUM_GENERIC_STAGES = 7,
	NUMBER_OF_TRANSPARENT_TYPES = 4,
	NUMBER_OF_BLEND_FUNCTIONS = 8,
	NUMBER_OF_FADE_MODES = 3,
	NUMBER_OF_PERIODIC_FUNCTIONS = 12,
	NUMBER_OF_STAGE_INPUTS = 25,
	NUMBER_OF_STAGE_INPUT_MAPPINGS = 8,
	NUMBER_OF_STAGE_OUTPUTS = 9,
	NUMBER_OF_STAGE_OUTPUT_FUNCTIONS = 2,
	NUMBER_OF_STAGE_OUTPUT_MAPPINGS = 6,

	/* the bitmaps taken: one for each map, and their blocks */
	MAXIMUM_TELEPORTER_BITMAPS = MAXIMUM_GENERIC_MAPS,
	MAXIMUM_SEQUENCES = 256,
	MAXIMUM_SPRITES = 256,
	MAXIMUM_BITMAPS = 64,
	MAXIMUM_PIXELS_BYTES = 0x1600000
};

/* ---------- structures */

/* (cache_files.c's) */
struct cache_file_tag_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

typedef char verify_xbox_teleporters_instance_size[sizeof(struct cache_file_tag_instance) == 0x20 ? 1 : -1];

/* a generic shader's map's and stage's fields that index tables (offsets in
them: tag_schema_render.c's shader_transparent_generic_map and _stage) */
struct teleporter_enum
{
	unsigned short offset;
	short count;
};

/* an Xbox map, inflated */
struct xbox_map
{
	byte *file;
	unsigned long size;
	unsigned long tag_offset;
	unsigned long tag_size;
	unsigned long instances;
	long tag_count;
};

/* a bitmap taken: its group (blocks its own) and pixels */
struct teleporter_bitmap
{
	long xbox_index;
	char name[256];
	struct bitmap_group *group;
	byte **pixels;
};

/* ---------- globals */

static struct
{
	/* the Xbox's shader, read once a run (or failed) */
	boolean read;
	boolean failed;
	byte *shader;
	long bitmap_count;
	struct teleporter_bitmap bitmaps[MAXIMUM_TELEPORTER_BITMAPS];
	/* each of the shader's maps' bitmap (an index in bitmaps), or NONE */
	short map_bitmaps[MAXIMUM_GENERIC_MAPS];
	/* the map's tags while it has it: its table, and the one before */
	boolean installed;
	struct cache_file_tag_instance *instances;
	struct cache_file_tag_instance *original_instances;
	long original_count;
	long handles[MAXIMUM_TELEPORTER_BITMAPS];
} xbox_teleporters;

static struct teleporter_enum const generic_map_enums[] =
{
	/* (its animation's u, v and rotation sources and functions) */
	{ 0x2C, NUMBER_OF_OBJECT_FUNCTION_REFERENCES }, { 0x2E, NUMBER_OF_PERIODIC_FUNCTIONS },
	{ 0x3C, NUMBER_OF_OBJECT_FUNCTION_REFERENCES }, { 0x3E, NUMBER_OF_PERIODIC_FUNCTIONS },
	{ 0x4C, NUMBER_OF_OBJECT_FUNCTION_REFERENCES }, { 0x4E, NUMBER_OF_PERIODIC_FUNCTIONS },
};

static struct teleporter_enum const generic_stage_enums[] =
{
	{ 0x04, NUMBER_OF_OBJECT_FUNCTION_REFERENCES }, { 0x06, NUMBER_OF_PERIODIC_FUNCTIONS },
	/* (the color inputs A to D and their mappings) */
	{ 0x3C, NUMBER_OF_STAGE_INPUTS }, { 0x3E, NUMBER_OF_STAGE_INPUT_MAPPINGS },
	{ 0x40, NUMBER_OF_STAGE_INPUTS }, { 0x42, NUMBER_OF_STAGE_INPUT_MAPPINGS },
	{ 0x44, NUMBER_OF_STAGE_INPUTS }, { 0x46, NUMBER_OF_STAGE_INPUT_MAPPINGS },
	{ 0x48, NUMBER_OF_STAGE_INPUTS }, { 0x4A, NUMBER_OF_STAGE_INPUT_MAPPINGS },
	/* (the color outputs, their functions, and the mapping) */
	{ 0x4C, NUMBER_OF_STAGE_OUTPUTS }, { 0x4E, NUMBER_OF_STAGE_OUTPUT_FUNCTIONS },
	{ 0x50, NUMBER_OF_STAGE_OUTPUTS }, { 0x52, NUMBER_OF_STAGE_OUTPUT_FUNCTIONS },
	{ 0x54, NUMBER_OF_STAGE_OUTPUTS }, { 0x56, NUMBER_OF_STAGE_OUTPUT_MAPPINGS },
	/* (the alpha inputs, outputs and mapping) */
	{ 0x58, NUMBER_OF_STAGE_INPUTS }, { 0x5A, NUMBER_OF_STAGE_INPUT_MAPPINGS },
	{ 0x5C, NUMBER_OF_STAGE_INPUTS }, { 0x5E, NUMBER_OF_STAGE_INPUT_MAPPINGS },
	{ 0x60, NUMBER_OF_STAGE_INPUTS }, { 0x62, NUMBER_OF_STAGE_INPUT_MAPPINGS },
	{ 0x64, NUMBER_OF_STAGE_INPUTS }, { 0x66, NUMBER_OF_STAGE_INPUT_MAPPINGS },
	{ 0x68, NUMBER_OF_STAGE_OUTPUTS }, { 0x6A, NUMBER_OF_STAGE_OUTPUTS },
	{ 0x6C, NUMBER_OF_STAGE_OUTPUTS }, { 0x6E, NUMBER_OF_STAGE_OUTPUT_MAPPINGS },
};

/* the game's own multiplayer maps, looked in for the Xbox's shader */
static char const *const xbox_map_names[] =
{
	"bloodgulch", "sidewinder", "beavercreek", "boardingaction", "carousel", "chillout", "damnation",
	"hangemhigh", "longest", "prisoner", "putput", "ratrace", "wizard"
};

/* ---------- prototypes */

int config_boolean(char const *name);
void *cache_files_tag_instances(long *count);
void cache_files_set_tag_instances(void *instances, long count);
void halo_custom_edition_texels_channels(void const *texels, unsigned char channel_order);

/* ---------- private code */

static unsigned long read_long(
	byte const *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

/* `size` bytes at the Xbox address `address` of the map's tags, or NULL if
they are not all in them */
static void *xbox_tags_at(
	struct xbox_map const *map,
	unsigned long address,
	unsigned long size)
{
	unsigned long offset = address - XBOX_TAG_BASE;

	if (address < XBOX_TAG_BASE || offset > map->tag_size || size > map->tag_size - offset)
		return NULL;
	return map->file + map->tag_offset + offset;
}

/* the map `name` of the game's maps folder, inflated (FALSE: not one) */
static boolean xbox_map_read(
	char const *name,
	struct xbox_map *map)
{
	char path[256];
	FILE *stream;
	byte *compressed = NULL;
	long compressed_size = 0;
	boolean read = FALSE;
	z_stream inflation;

	memset(map, 0, sizeof(*map));
	snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), name);
	stream = fopen(path, "rb");
	if (!stream)
		return FALSE;
	if (!fseek(stream, 0, SEEK_END) && (compressed_size = ftell(stream)) > XBOX_CACHE_HEADER_BYTES &&
		compressed_size <= XBOX_MAXIMUM_MAP_BYTES && !fseek(stream, 0, SEEK_SET) &&
		(compressed = malloc((size_t)compressed_size)) != NULL)
	{
		read = fread(compressed, 1, (size_t)compressed_size, stream) == (size_t)compressed_size;
	}
	fclose(stream);
	if (!read || read_long(compressed) != XBOX_CACHE_SIGNATURE || read_long(compressed + 4) != XBOX_CACHE_VERSION)
	{
		if (compressed)
			free(compressed);
		return FALSE;
	}
	map->size = read_long(compressed + 8);
	map->tag_offset = read_long(compressed + 0x10);
	map->tag_size = read_long(compressed + 0x14);
	read = FALSE;
	if (map->size > XBOX_CACHE_HEADER_BYTES && map->size <= XBOX_MAXIMUM_MAP_BYTES && map->tag_offset <= map->size &&
		map->tag_size <= map->size - map->tag_offset && (map->file = malloc(map->size)) != NULL)
	{
		memcpy(map->file, compressed, XBOX_CACHE_HEADER_BYTES);
		memset(&inflation, 0, sizeof(inflation));
		if (inflateInit(&inflation) == Z_OK)
		{
			int result;

			inflation.next_in = compressed + XBOX_CACHE_HEADER_BYTES;
			inflation.avail_in = (uInt)(compressed_size - XBOX_CACHE_HEADER_BYTES);
			inflation.next_out = map->file + XBOX_CACHE_HEADER_BYTES;
			inflation.avail_out = (uInt)(map->size - XBOX_CACHE_HEADER_BYTES);
			result = inflate(&inflation, Z_FINISH);
			read = (result == Z_STREAM_END || result == Z_OK || result == Z_BUF_ERROR) &&
				inflation.total_out == map->size - XBOX_CACHE_HEADER_BYTES;
			inflateEnd(&inflation);
		}
	}
	free(compressed);
	if (read)
	{
		byte const *header = xbox_tags_at(map, XBOX_TAG_BASE, 0x10);

		map->instances = header ? read_long(header) : 0;
		map->tag_count = header ? (long)read_long(header + 0x0C) : 0;
		read = header && map->tag_count > 0 && map->tag_count <= UNSIGNED_SHORT_MAX &&
			xbox_tags_at(map, map->instances, (unsigned long)map->tag_count * XBOX_TAG_INSTANCE_BYTES) != NULL;
	}
	if (!read && map->file)
	{
		free(map->file);
		map->file = NULL;
	}

	return read;
}

/* the map's tag instance `index` (absolute), or NULL */
static struct cache_file_tag_instance const *xbox_instance(
	struct xbox_map const *map,
	long index)
{
	if (index < 0 || index >= map->tag_count)
		return NULL;
	return (struct cache_file_tag_instance const *)xbox_tags_at(map,
		map->instances + (unsigned long)index * XBOX_TAG_INSTANCE_BYTES, XBOX_TAG_INSTANCE_BYTES);
}

/* the instance's name (whole, in the tags), or NULL */
static char const *xbox_instance_name(
	struct xbox_map const *map,
	struct cache_file_tag_instance const *instance)
{
	char const *name = xbox_tags_at(map, (unsigned long)instance->name, 1);
	unsigned long left = name ? map->tag_size - ((unsigned long)instance->name - XBOX_TAG_BASE) : 0;

	return name && memchr(name, 0, left) ? name : NULL;
}

/* the absolute index of the map's tag of `group_tag` named `name`, or NONE */
static long xbox_tag_find(
	struct xbox_map const *map,
	unsigned long group_tag,
	char const *name)
{
	long index;

	for (index = 0; index < map->tag_count; index++)
	{
		struct cache_file_tag_instance const *instance = xbox_instance(map, index);
		char const *instance_name;

		if (!instance || instance->group_tag != group_tag)
			continue;
		instance_name = xbox_instance_name(map, instance);
		if (instance_name && !strcmp(instance_name, name))
			return index;
	}

	return NONE;
}

/* a copy of the block's `count` elements of `element_bytes` each (at most
`maximum`), its address made the copy's: FALSE if they are not all in the
tags */
static boolean xbox_block_copy(
	struct xbox_map const *map,
	struct tag_block *block,
	unsigned long element_bytes,
	long maximum)
{
	void const *source;
	void *copy;

	block->definition = NULL;
	if (block->count < 0 || block->count > maximum)
		return FALSE;
	if (!block->count)
	{
		block->address = NULL;
		return TRUE;
	}
	source = xbox_tags_at(map, (unsigned long)block->address, (unsigned long)block->count * element_bytes);
	copy = source ? malloc((size_t)block->count * element_bytes) : NULL;
	if (!copy)
		return FALSE;
	memcpy(copy, source, (size_t)block->count * element_bytes);
	block->address = copy;

	return TRUE;
}

/* the fields of `count` elements (`element_bytes` each, at `elements`) that
index tables, each made the first of its table when it is none of it */
static void teleporter_enums_clamp(
	byte *elements,
	long count,
	unsigned long element_bytes,
	struct teleporter_enum const *enums,
	short enum_count)
{
	long element;
	short index;

	for (element = 0; element < count; element++)
	{
		for (index = 0; index < enum_count; index++)
		{
			short *value = (short *)(elements + element * element_bytes + enums[index].offset);

			if (*value < 0 || *value >= enums[index].count)
				*value = 0;
		}
	}
}

/* the bitmap of absolute index `xbox_index` taken (its blocks and pixels
copied): FALSE if it is not one, or not all in what was read */
static boolean teleporter_bitmap_take(
	struct xbox_map const *map,
	long xbox_index,
	struct teleporter_bitmap *bitmap)
{
	struct cache_file_tag_instance const *instance = xbox_instance(map, xbox_index);
	char const *name = instance ? xbox_instance_name(map, instance) : NULL;
	struct bitmap_group const *source;
	struct bitmap_group *group;
	long index;

	memset(bitmap, 0, sizeof(*bitmap));
	bitmap->xbox_index = xbox_index;
	if (!instance || instance->group_tag != 'bitm' || !name || strlen(name) >= sizeof(bitmap->name))
		return FALSE;
	strcpy(bitmap->name, name);
	source = xbox_tags_at(map, (unsigned long)instance->base_address, sizeof(*group));
	group = source ? malloc(sizeof(*group)) : NULL;
	if (!group)
		return FALSE;
	memcpy(group, source, sizeof(*group));
	bitmap->group = group;
	memset(&group->import_bitmap, 0, sizeof(group->import_bitmap));
	memset(&group->pixel_data, 0, sizeof(group->pixel_data));
	if (!xbox_block_copy(map, &group->sequences, sizeof(struct bitmap_group_sequence), MAXIMUM_SEQUENCES))
	{
		group->sequences.count = 0;
		group->bitmaps.count = 0;
		return FALSE;
	}
	for (index = 0; index < group->sequences.count; index++)
	{
		struct bitmap_group_sequence *sequence = (struct bitmap_group_sequence *)group->sequences.address + index;

		if (!xbox_block_copy(map, &sequence->sprites, sizeof(struct bitmap_group_sprite), MAXIMUM_SPRITES))
		{
			/* (those not copied are none, freed as none) */
			for (; index < group->sequences.count; index++)
				((struct bitmap_group_sequence *)group->sequences.address)[index].sprites.count = 0;
			group->bitmaps.count = 0;
			return FALSE;
		}
	}
	if (!xbox_block_copy(map, &group->bitmaps, sizeof(struct bitmap_data), MAXIMUM_BITMAPS))
	{
		group->bitmaps.count = 0;
		return FALSE;
	}
	bitmap->pixels = group->bitmaps.count ? calloc((size_t)group->bitmaps.count, sizeof(byte *)) : NULL;
	if (group->bitmaps.count && !bitmap->pixels)
		return FALSE;
	for (index = 0; index < group->bitmaps.count; index++)
	{
		struct bitmap_data *data = (struct bitmap_data *)group->bitmaps.address + index;

		if (data->pixels_offset < 0 || data->pixels_size <= 0 || data->pixels_size > MAXIMUM_PIXELS_BYTES ||
			(unsigned long)data->pixels_offset > map->size ||
			(unsigned long)data->pixels_size > map->size - (unsigned long)data->pixels_offset ||
			(bitmap->pixels[index] = malloc((size_t)data->pixels_size)) == NULL)
		{
			return FALSE;
		}
		memcpy(bitmap->pixels[index], map->file + data->pixels_offset, (size_t)data->pixels_size);
		data->tag_index = NONE;
		data->cache_block_index = NONE;
		data->hardware_format = NULL;
		data->base_address = NULL;
	}

	return TRUE;
}

static void teleporter_bitmap_free(
	struct teleporter_bitmap *bitmap)
{
	long index;

	if (bitmap->group)
	{
		for (index = 0; bitmap->pixels && index < bitmap->group->bitmaps.count; index++)
		{
			if (bitmap->pixels[index])
				free(bitmap->pixels[index]);
		}
		for (index = 0; index < bitmap->group->sequences.count; index++)
		{
			struct bitmap_group_sequence *sequence = (struct bitmap_group_sequence *)bitmap->group->sequences.address + index;

			if (sequence->sprites.address)
				free(sequence->sprites.address);
		}
		if (bitmap->group->sequences.address)
			free(bitmap->group->sequences.address);
		if (bitmap->group->bitmaps.address)
			free(bitmap->group->bitmaps.address);
		free(bitmap->group);
	}
	if (bitmap->pixels)
		free(bitmap->pixels);
	memset(bitmap, 0, sizeof(*bitmap));
}

/* the Xbox's shader taken from the map (its blocks copied, its bitmaps
taken), or FALSE */
static boolean teleporter_shader_take(
	struct xbox_map const *map)
{
	long shader_index = xbox_tag_find(map, 'sotr', TELEPORTER_SHADER_NAME);
	struct cache_file_tag_instance const *instance = xbox_instance(map, shader_index);
	byte const *source = instance ? xbox_tags_at(map, (unsigned long)instance->base_address, GENERIC_SHADER_BYTES) : NULL;
	struct tag_block *maps, *stages, *layers;
	struct tag_reference *lens_flare;
	byte *shader;
	long index;

	if (!source || (shader = malloc(GENERIC_SHADER_BYTES)) == NULL)
		return FALSE;
	memcpy(shader, source, GENERIC_SHADER_BYTES);
	xbox_teleporters.shader = shader;
	layers = (struct tag_block *)(shader + 0x48);
	maps = (struct tag_block *)(shader + 0x54);
	stages = (struct tag_block *)(shader + 0x60);
	lens_flare = (struct tag_reference *)(shader + 0x38);
	/* (its extra layers and lens flare are other tags, not taken) */
	layers->count = 0;
	layers->address = NULL;
	lens_flare->name = "";
	lens_flare->name_length = 0;
	lens_flare->index = NONE;
	if (*(short *)(shader + 0x24) != GENERIC_SHADER_TYPE ||
		!xbox_block_copy(map, maps, GENERIC_MAP_BYTES, MAXIMUM_GENERIC_MAPS))
	{
		maps->count = 0;
		stages->count = 0;
		return FALSE;
	}
	if (!xbox_block_copy(map, stages, GENERIC_STAGE_BYTES, MAXIMUM_GENERIC_STAGES))
	{
		stages->count = 0;
		return FALSE;
	}
	/* (the transparent shader's first map type, blend, fade mode and source,
	and each map's and stage's fields that index tables) */
	if (*(unsigned short *)(shader + 0x2A) >= NUMBER_OF_TRANSPARENT_TYPES)
		*(short *)(shader + 0x2A) = 0;
	if (*(unsigned short *)(shader + 0x2C) >= NUMBER_OF_BLEND_FUNCTIONS)
		*(short *)(shader + 0x2C) = 0;
	if (*(unsigned short *)(shader + 0x2E) >= NUMBER_OF_FADE_MODES)
		*(short *)(shader + 0x2E) = 0;
	if (*(unsigned short *)(shader + 0x30) >= NUMBER_OF_OBJECT_FUNCTION_REFERENCES)
		*(short *)(shader + 0x30) = 0;
	teleporter_enums_clamp(maps->address, maps->count, GENERIC_MAP_BYTES, generic_map_enums, NUMBEROF(generic_map_enums));
	teleporter_enums_clamp(stages->address, stages->count, GENERIC_STAGE_BYTES, generic_stage_enums,
		NUMBEROF(generic_stage_enums));

	/* its maps' bitmaps, each once */
	for (index = 0; index < maps->count; index++)
	{
		struct tag_reference *reference = (struct tag_reference *)((byte *)maps->address + index * GENERIC_MAP_BYTES + 0x1C);
		long xbox_index = reference->index == NONE ? NONE : (long)((unsigned long)reference->index & 0xFFFF);
		long bitmap;

		xbox_teleporters.map_bitmaps[index] = NONE;
		if (xbox_index == NONE)
			continue;
		for (bitmap = 0; bitmap < xbox_teleporters.bitmap_count && xbox_teleporters.bitmaps[bitmap].xbox_index != xbox_index;
			bitmap++)
			;
		if (bitmap == xbox_teleporters.bitmap_count &&
			!teleporter_bitmap_take(map, xbox_index, &xbox_teleporters.bitmaps[xbox_teleporters.bitmap_count++]))
		{
			return FALSE;
		}
		xbox_teleporters.map_bitmaps[index] = (short)bitmap;
	}

	return TRUE;
}

/* the Xbox's shader read from the first of the game's maps with it, once a
run: FALSE if none has it */
static boolean xbox_teleporters_read_shader(
	void)
{
	short map_index;

	if (xbox_teleporters.read)
		return !xbox_teleporters.failed;
	xbox_teleporters.read = TRUE;
	xbox_teleporters.failed = TRUE;
	for (map_index = 0; map_index < NUMBEROF(xbox_map_names); map_index++)
	{
		struct xbox_map map;
		boolean taken;
		long bitmap;

		if (!xbox_map_read(xbox_map_names[map_index], &map))
			continue;
		if (xbox_tag_find(&map, 'sotr', TELEPORTER_SHADER_NAME) == NONE)
		{
			free(map.file);
			continue;
		}
		taken = teleporter_shader_take(&map);
		free(map.file);
		if (taken)
		{
			xbox_teleporters.failed = FALSE;
			error(_error_silent, "xbox teleporters: the Xbox's teleporter field read from %s.map (%ld bitmaps)",
				xbox_map_names[map_index], xbox_teleporters.bitmap_count);
			return TRUE;
		}
		/* (what was taken of it let go) */
		for (bitmap = 0; bitmap < xbox_teleporters.bitmap_count; bitmap++)
			teleporter_bitmap_free(&xbox_teleporters.bitmaps[bitmap]);
		xbox_teleporters.bitmap_count = 0;
		if (xbox_teleporters.shader)
		{
			struct tag_block *maps = (struct tag_block *)(xbox_teleporters.shader + 0x54);
			struct tag_block *stages = (struct tag_block *)(xbox_teleporters.shader + 0x60);

			if (maps->count && maps->address)
				free(maps->address);
			if (stages->count && stages->address)
				free(stages->address);
			free(xbox_teleporters.shader);
			xbox_teleporters.shader = NULL;
		}
		error(_error_silent, "xbox teleporters: %s.map's teleporter field is damaged", xbox_map_names[map_index]);
		break;
	}
	error(_error_silent, "xbox teleporters: none of the game's maps has the Xbox's teleporter field: Halo PC's is drawn");

	return FALSE;
}

/* ---------- public code */

void xbox_teleporters_tags_loaded(
	void)
{
	struct cache_file_tag_instance *instances;
	long count, shader_index, index, first_salt = 0;
	struct tag_block *maps;

	if (xbox_teleporters.installed || !custom_edition_cache_tags_loaded() ||
		!config_boolean("display.xbox_teleporters"))
	{
		return;
	}
	shader_index = tag_loaded('schi', TELEPORTER_SHADER_NAME);
	if (shader_index == NONE)
		shader_index = tag_loaded('scex', TELEPORTER_SHADER_NAME);
	if (shader_index == NONE || !xbox_teleporters_read_shader())
		return;
	instances = cache_files_tag_instances(&count);
	if (!instances || count <= 0 || count + xbox_teleporters.bitmap_count > UNSIGNED_SHORT_MAX ||
		DATUM_INDEX_TO_ABSOLUTE_INDEX(shader_index) >= count)
	{
		return;
	}
	xbox_teleporters.instances = malloc((size_t)(count + xbox_teleporters.bitmap_count) * sizeof(*instances));
	if (!xbox_teleporters.instances)
		return;
	memcpy(xbox_teleporters.instances, instances, (size_t)count * sizeof(*instances));
	for (index = 0; index < count; index++)
	{
		long salt = (long)((unsigned long)instances[index].tag_index >> 16);

		if (instances[index].tag_index != NONE && salt > first_salt)
			first_salt = salt;
	}
	first_salt++;
	if (first_salt + xbox_teleporters.bitmap_count >= UNSIGNED_SHORT_MAX)
	{
		free(xbox_teleporters.instances);
		xbox_teleporters.instances = NULL;
		return;
	}
	/* the bitmaps, after the map's tags (and the menus') */
	for (index = 0; index < xbox_teleporters.bitmap_count; index++)
	{
		struct teleporter_bitmap *bitmap = &xbox_teleporters.bitmaps[index];
		struct cache_file_tag_instance *instance = &xbox_teleporters.instances[count + index];
		long data_index;

		xbox_teleporters.handles[index] = ((first_salt + index) << 16) | (count + index);
		memset(instance, 0, sizeof(*instance));
		instance->group_tag = 'bitm';
		instance->parent_group_tags[0] = NONE;
		instance->parent_group_tags[1] = NONE;
		instance->tag_index = xbox_teleporters.handles[index];
		instance->name = bitmap->name;
		instance->base_address = bitmap->group;
		for (data_index = 0; data_index < bitmap->group->bitmaps.count; data_index++)
		{
			struct bitmap_data *data = (struct bitmap_data *)bitmap->group->bitmaps.address + data_index;

			data->tag_index = xbox_teleporters.handles[index];
			data->cache_block_index = NONE;
			data->hardware_format = NULL;
			data->base_address = NULL;
		}
	}
	/* the shader's maps name them (by this map's handles) */
	maps = (struct tag_block *)(xbox_teleporters.shader + 0x54);
	for (index = 0; index < maps->count; index++)
	{
		struct tag_reference *reference = (struct tag_reference *)((byte *)maps->address + index * GENERIC_MAP_BYTES + 0x1C);
		short bitmap = xbox_teleporters.map_bitmaps[index];

		reference->group_tag = 'bitm';
		reference->name = bitmap != NONE ? xbox_teleporters.bitmaps[bitmap].name : "";
		reference->name_length = (long)strlen(reference->name);
		reference->index = bitmap != NONE ? xbox_teleporters.handles[bitmap] : NONE;
	}
	/* and the shader's tag is the Xbox's */
	{
		struct cache_file_tag_instance *instance =
			&xbox_teleporters.instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(shader_index)];

		instance->group_tag = 'sotr';
		instance->base_address = xbox_teleporters.shader;
	}
	xbox_teleporters.original_instances = instances;
	xbox_teleporters.original_count = count;
	xbox_teleporters.installed = TRUE;
	cache_files_set_tag_instances(xbox_teleporters.instances, count + xbox_teleporters.bitmap_count);

	return;
}

void xbox_teleporters_tags_unloaded(
	void)
{
	if (!xbox_teleporters.installed)
		return;
	cache_files_set_tag_instances(xbox_teleporters.original_instances, xbox_teleporters.original_count);
	free(xbox_teleporters.instances);
	xbox_teleporters.instances = NULL;
	xbox_teleporters.original_instances = NULL;
	xbox_teleporters.installed = FALSE;

	return;
}

boolean xbox_teleporters_read(
	long tag_index,
	long offset,
	long size,
	void *buffer)
{
	long bitmap_index;

	if (!xbox_teleporters.installed || tag_index == NONE)
		return FALSE;
	for (bitmap_index = 0; bitmap_index < xbox_teleporters.bitmap_count; bitmap_index++)
	{
		struct teleporter_bitmap const *bitmap = &xbox_teleporters.bitmaps[bitmap_index];
		long data_index;

		if (xbox_teleporters.handles[bitmap_index] != tag_index)
			continue;
		for (data_index = 0; data_index < bitmap->group->bitmaps.count; data_index++)
		{
			struct bitmap_data const *data = (struct bitmap_data const *)bitmap->group->bitmaps.address + data_index;

			if (offset >= data->pixels_offset && size >= 0 && offset - data->pixels_offset <= data->pixels_size &&
				size <= data->pixels_size - (offset - data->pixels_offset))
			{
				memcpy(buffer, bitmap->pixels[data_index] + (offset - data->pixels_offset), (size_t)size);
				/* (the renderer told the address holds the Xbox's channels:
				it keeps the order of a Halo PC bitmap read there before) */
				halo_custom_edition_texels_channels(buffer, 0);
				return TRUE;
			}
		}
		/* (a read of none of its pixels reads nothing) */
		memset(buffer, 0, size > 0 ? (size_t)size : 0);
		return TRUE;
	}

	return FALSE;
}
