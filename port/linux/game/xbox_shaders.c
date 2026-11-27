/*
XBOX_SHADERS.C

The Xbox's shaders on Halo PC's maps (custom-387). The Xbox draws some
surfaces with generic transparent shaders ('sotr', a type Halo PC has not),
which Gearbox drew on Halo PC with transparent chicago extended shaders made
to look like them (which this build reads as chicago shaders):

- the teleporters' field (TELEPORTER_SHADER_NAME), on the Xbox a swirl of
  energy, on Halo PC a green frame added over moving dust
  (display.xbox_teleporters);
- the Covenant field generator's shield (FIELD_SHIELD_SHADER_NAME, on Danger
  Canyon and Gephyrophobia), on the Xbox's campaign two layers of a jackal
  shield's moving noise bent by an offset map, edged by a mask, in 7 stages,
  on Halo PC two layers of moving dust in the mask;
- Gephyrophobia's bridge's energy ropes (ENERGY_ROPE_SHADER_NAME), on Halo
  PC a copy of b40's ropes, whose bitmaps it draws, which the Xbox's b40
  draws with ENERGY_ROPE_XBOX_SHADER_NAME (the two with
  display.xbox_campaign_shaders).

When a Halo PC (or Custom Edition) map with one of those loads, the Xbox's
is read from the first of the game's own maps listed for it that has it
(inflated as the game inflates them), with its bitmaps and their pixels,
and stands in for Halo PC's: the shader's tag becomes the Xbox's, which every
reference to it then finds, the bitmaps are added to the map's tags (after
the menus' own, menu_tags.c, and taken away before them), and their pixels
are served from what was read (cache_file_read). Each is read once a run.
Like the other changes of custom-387 to Halo PC's maps, these pick tags by
name.

The Xbox map is the player's file: everything taken from it is checked to
be in what was read, the shader's blocks no longer than the game's, and its
values that index the renderer's tables in their ranges, as the tag
validator checks a map's (tag_schema_render.c); the bitmaps' fields are the
texture cache's to check, as a map's are (xbox_texture_cache.c). What
cannot be read leaves Halo PC's shader (logged once).
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "bitmaps/bitmap_group.h"
#include "cache/cache_files.h"
#include "tag_files/tag_groups.h"
#include "objects/objects.h"
#include "custom_edition_cache.h"
#include "xbox_shaders.h"
#include "../../third_party/zlib/zlib_prefixed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

#define TELEPORTER_SHADER_NAME "scenery\\teleporter_shield\\shaders\\teleporter_shield"
#define FIELD_SHIELD_SHADER_NAME "scenery\\c_field_generator\\shaders\\generator shield"
#define ENERGY_ROPE_SHADER_NAME "levels\\test\\gephyrophobia\\shaders\\geph_energy_rope"
#define ENERGY_ROPE_XBOX_SHADER_NAME "levels\\b40\\shaders\\b40_energyrope"

enum
{
	XBOX_CACHE_HEADER_BYTES = 0x800,
	XBOX_CACHE_VERSION = 5,
	XBOX_CACHE_SIGNATURE = 'head',
	/* (the Xbox's tag data's address: cache_files.c) */
	XBOX_TAG_BASE = 0x803A6000,
	XBOX_TAG_INSTANCE_BYTES = 0x20,
	/* the most of an Xbox map inflated, and of its tags */
	XBOX_MAXIMUM_MAP_BYTES = 0x20000000,
	XBOX_MAXIMUM_TAG_BYTES = 0x4000000,
	/* the pieces of a map read and inflated at once */
	XBOX_READ_BYTES = 0x40000,
	XBOX_INFLATE_BYTES = 0x100000,

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

	/* the bitmaps a shader takes: one for each map, and their blocks */
	MAXIMUM_SHADER_BITMAPS = MAXIMUM_GENERIC_MAPS,
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

typedef char verify_xbox_shaders_instance_size[sizeof(struct cache_file_tag_instance) == 0x20 ? 1 : -1];

/* a generic shader's map's and stage's fields that index tables (offsets in
them: tag_schema_render.c's shader_transparent_generic_map and _stage) */
struct shader_enum
{
	unsigned short offset;
	short count;
};

/* an Xbox map's tags, inflated */
struct xbox_map
{
	char const *name;
	unsigned long size;
	unsigned long tag_offset;
	unsigned long tag_size;
	byte *tags;
	unsigned long instances;
	long tag_count;
};

/* a piece of an Xbox map's inflated file wanted: `size` bytes from `offset`,
into `bytes` */
struct xbox_map_piece
{
	unsigned long offset;
	unsigned long size;
	byte *bytes;
};

/* a bitmap taken: its group (blocks its own) and pixels */
struct shader_bitmap
{
	long xbox_index;
	char name[256];
	struct bitmap_group *group;
	byte **pixels;
};

/* a Halo PC shader the Xbox's stands in for */
struct xbox_shader_swap
{
	/* its name on Halo PC's maps, and the Xbox's */
	char const *halo_pc_name;
	char const *xbox_name;
	/* the setting that turns it off */
	char const *setting;
	/* the game's maps looked in for the Xbox's, in order */
	char const *const *xbox_maps;
	short xbox_map_count;
};

/* the Xbox's shader of a swap: read once a run (or failed), and while a map
has it, the handles of its bitmaps there */
struct xbox_shader
{
	boolean read;
	boolean failed;
	byte *shader;
	long bitmap_count;
	struct shader_bitmap bitmaps[MAXIMUM_SHADER_BITMAPS];
	/* each of the shader's maps' bitmap (an index in bitmaps), or NONE */
	short map_bitmaps[MAXIMUM_GENERIC_MAPS];
	boolean installed;
	long handles[MAXIMUM_SHADER_BITMAPS];
};

/* ---------- globals */

/* the game's own multiplayer maps, and its campaign's with a field generator
(the smallest first: each is inflated whole to find its tags) */
static char const *const multiplayer_map_names[] =
{
	"bloodgulch", "sidewinder", "beavercreek", "boardingaction", "carousel", "chillout", "damnation",
	"hangemhigh", "longest", "prisoner", "putput", "ratrace", "wizard"
};
static char const *const field_generator_map_names[] =
{
	"c20", "b30", "d20", "a50", "c10", "c40", "a10", "b40", "d40"
};
static char const *const energy_rope_map_names[] =
{
	"b40"
};

static struct xbox_shader_swap const xbox_shader_swaps[] =
{
	{ TELEPORTER_SHADER_NAME, TELEPORTER_SHADER_NAME, "display.xbox_teleporters",
		multiplayer_map_names, NUMBEROF(multiplayer_map_names) },
	{ FIELD_SHIELD_SHADER_NAME, FIELD_SHIELD_SHADER_NAME, "display.xbox_campaign_shaders",
		field_generator_map_names, NUMBEROF(field_generator_map_names) },
	{ ENERGY_ROPE_SHADER_NAME, ENERGY_ROPE_XBOX_SHADER_NAME, "display.xbox_campaign_shaders",
		energy_rope_map_names, NUMBEROF(energy_rope_map_names) },
};

#define NUMBER_OF_XBOX_SHADER_SWAPS ((short)NUMBEROF(xbox_shader_swaps))

static struct
{
	struct xbox_shader shaders[NUMBEROF(xbox_shader_swaps)];
	/* the map's tags while it has any: its table, and the one before */
	boolean installed;
	struct cache_file_tag_instance *instances;
	struct cache_file_tag_instance *original_instances;
	long original_count;
} xbox_shaders;

static struct shader_enum const generic_map_enums[] =
{
	/* (its animation's u, v and rotation sources and functions) */
	{ 0x2C, NUMBER_OF_OBJECT_FUNCTION_REFERENCES }, { 0x2E, NUMBER_OF_PERIODIC_FUNCTIONS },
	{ 0x3C, NUMBER_OF_OBJECT_FUNCTION_REFERENCES }, { 0x3E, NUMBER_OF_PERIODIC_FUNCTIONS },
	{ 0x4C, NUMBER_OF_OBJECT_FUNCTION_REFERENCES }, { 0x4E, NUMBER_OF_PERIODIC_FUNCTIONS },
};

static struct shader_enum const generic_stage_enums[] =
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
	return map->tags + offset;
}

/* the map `name` of the game's maps folder inflated from its start, as the
game inflates it, the `piece_count` pieces asked for copied out as they go
by, up to the end of the last (a map's file is a header and the rest
deflated: its tags at the end, the pixels before them); FALSE if it is not
an Xbox map whose inflated file holds them all. A map can be larger
inflated than this build has room for at once (b40, 280 MB): it is never
held whole. */
static boolean xbox_map_pieces_read(
	char const *name,
	struct xbox_map_piece const *pieces,
	long piece_count)
{
	char path[256];
	FILE *stream;
	byte header[XBOX_CACHE_HEADER_BYTES];
	byte *input = NULL;
	byte *output = NULL;
	unsigned long size, end = 0, at;
	boolean read = FALSE;
	z_stream inflation;
	long index;

	snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), name);
	stream = fopen(path, "rb");
	if (!stream)
		return FALSE;
	if (fread(header, 1, sizeof(header), stream) != sizeof(header) ||
		read_long(header) != XBOX_CACHE_SIGNATURE || read_long(header + 4) != XBOX_CACHE_VERSION ||
		(size = read_long(header + 8)) <= XBOX_CACHE_HEADER_BYTES || size > XBOX_MAXIMUM_MAP_BYTES)
	{
		fclose(stream);
		return FALSE;
	}
	for (index = 0; index < piece_count; index++)
	{
		if (pieces[index].offset > size || pieces[index].size > size - pieces[index].offset)
		{
			fclose(stream);
			return FALSE;
		}
		end = MAX(end, pieces[index].offset + pieces[index].size);
	}
	/* (what the header holds of them) */
	for (index = 0; index < piece_count; index++)
	{
		struct xbox_map_piece const *piece = &pieces[index];

		if (piece->offset < XBOX_CACHE_HEADER_BYTES)
			memcpy(piece->bytes, header + piece->offset, MIN(piece->size, XBOX_CACHE_HEADER_BYTES - piece->offset));
	}
	input = malloc(XBOX_READ_BYTES);
	output = malloc(XBOX_INFLATE_BYTES);
	memset(&inflation, 0, sizeof(inflation));
	if (input && output && inflateInit(&inflation) == Z_OK)
	{
		at = XBOX_CACHE_HEADER_BYTES;
		read = TRUE;
		while (read && at < end)
		{
			unsigned long produced;
			int result;

			if (inflation.avail_in == 0)
			{
				size_t count = fread(input, 1, XBOX_READ_BYTES, stream);

				if (!count)
				{
					read = FALSE;
					break;
				}
				inflation.next_in = input;
				inflation.avail_in = (uInt)count;
			}
			inflation.next_out = output;
			inflation.avail_out = XBOX_INFLATE_BYTES;
			result = inflate(&inflation, Z_NO_FLUSH);
			produced = MIN(XBOX_INFLATE_BYTES - inflation.avail_out, size - at);
			/* (each piece's part of what came) */
			for (index = 0; index < piece_count; index++)
			{
				struct xbox_map_piece const *piece = &pieces[index];
				unsigned long from = MAX(piece->offset, at);
				unsigned long to = MIN(piece->offset + piece->size, at + produced);

				if (from < to)
					memcpy(piece->bytes + (from - piece->offset), output + (from - at), to - from);
			}
			at += produced;
			if (result == Z_STREAM_END)
				break;
			if ((result != Z_OK && result != Z_BUF_ERROR) || (result == Z_BUF_ERROR && !produced && inflation.avail_in))
				read = FALSE;
		}
		read = read && at >= end;
		inflateEnd(&inflation);
	}
	if (input)
		free(input);
	if (output)
		free(output);
	fclose(stream);

	return read;
}

/* the tags of the map `name` of the game's maps folder, inflated (FALSE:
not one) */
static boolean xbox_map_read(
	char const *name,
	struct xbox_map *map)
{
	char path[256];
	FILE *stream;
	byte header[XBOX_CACHE_HEADER_BYTES];
	struct xbox_map_piece piece;
	boolean read;

	memset(map, 0, sizeof(*map));
	map->name = name;
	snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), name);
	stream = fopen(path, "rb");
	if (!stream)
		return FALSE;
	read = fread(header, 1, sizeof(header), stream) == sizeof(header);
	fclose(stream);
	if (!read || read_long(header) != XBOX_CACHE_SIGNATURE || read_long(header + 4) != XBOX_CACHE_VERSION)
		return FALSE;
	map->size = read_long(header + 8);
	map->tag_offset = read_long(header + 0x10);
	map->tag_size = read_long(header + 0x14);
	if (map->size <= XBOX_CACHE_HEADER_BYTES || map->size > XBOX_MAXIMUM_MAP_BYTES ||
		map->tag_offset > map->size || map->tag_size > map->size - map->tag_offset ||
		map->tag_size < 0x10 || map->tag_size > XBOX_MAXIMUM_TAG_BYTES ||
		(map->tags = malloc(map->tag_size)) == NULL)
	{
		return FALSE;
	}
	piece.offset = map->tag_offset;
	piece.size = map->tag_size;
	piece.bytes = map->tags;
	read = xbox_map_pieces_read(name, &piece, 1);
	if (read)
	{
		byte const *tag_header = xbox_tags_at(map, XBOX_TAG_BASE, 0x10);

		map->instances = tag_header ? read_long(tag_header) : 0;
		map->tag_count = tag_header ? (long)read_long(tag_header + 0x0C) : 0;
		read = tag_header && map->tag_count > 0 && map->tag_count <= UNSIGNED_SHORT_MAX &&
			xbox_tags_at(map, map->instances, (unsigned long)map->tag_count * XBOX_TAG_INSTANCE_BYTES) != NULL;
	}
	if (!read)
	{
		free(map->tags);
		map->tags = NULL;
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
static void shader_enums_clamp(
	byte *elements,
	long count,
	unsigned long element_bytes,
	struct shader_enum const *enums,
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

/* the bitmap of absolute index `xbox_index` taken (its blocks copied, room
made for its pixels, read after: xbox_shader_pixels_read): FALSE if it is
not one, or not all in what was read */
static boolean shader_bitmap_take(
	struct xbox_map const *map,
	long xbox_index,
	struct shader_bitmap *bitmap)
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
		data->tag_index = NONE;
		data->cache_block_index = NONE;
		data->hardware_format = NULL;
		data->base_address = NULL;
	}

	return TRUE;
}

static void shader_bitmap_free(
	struct shader_bitmap *bitmap)
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

/* what was taken of a shader let go */
static void xbox_shader_free(
	struct xbox_shader *shader)
{
	long bitmap;

	for (bitmap = 0; bitmap < shader->bitmap_count; bitmap++)
		shader_bitmap_free(&shader->bitmaps[bitmap]);
	shader->bitmap_count = 0;
	if (shader->shader)
	{
		struct tag_block *maps = (struct tag_block *)(shader->shader + 0x54);
		struct tag_block *stages = (struct tag_block *)(shader->shader + 0x60);

		if (maps->count && maps->address)
			free(maps->address);
		if (stages->count && stages->address)
			free(stages->address);
		free(shader->shader);
		shader->shader = NULL;
	}
}

/* the Xbox's shader `name` taken from the map (its blocks copied, its
bitmaps taken), or FALSE */
static boolean xbox_shader_take(
	struct xbox_map const *map,
	char const *name,
	struct xbox_shader *taken)
{
	long shader_index = xbox_tag_find(map, 'sotr', name);
	struct cache_file_tag_instance const *instance = xbox_instance(map, shader_index);
	byte const *source = instance ? xbox_tags_at(map, (unsigned long)instance->base_address, GENERIC_SHADER_BYTES) : NULL;
	struct tag_block *maps, *stages, *layers;
	struct tag_reference *lens_flare;
	byte *shader;
	long index;

	if (!source || (shader = malloc(GENERIC_SHADER_BYTES)) == NULL)
		return FALSE;
	memcpy(shader, source, GENERIC_SHADER_BYTES);
	taken->shader = shader;
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
	shader_enums_clamp(maps->address, maps->count, GENERIC_MAP_BYTES, generic_map_enums, NUMBEROF(generic_map_enums));
	shader_enums_clamp(stages->address, stages->count, GENERIC_STAGE_BYTES, generic_stage_enums,
		NUMBEROF(generic_stage_enums));

	/* its maps' bitmaps, each once */
	for (index = 0; index < maps->count; index++)
	{
		struct tag_reference *reference = (struct tag_reference *)((byte *)maps->address + index * GENERIC_MAP_BYTES + 0x1C);
		long xbox_index = reference->index == NONE ? NONE : (long)((unsigned long)reference->index & 0xFFFF);
		long bitmap;

		taken->map_bitmaps[index] = NONE;
		if (xbox_index == NONE)
			continue;
		for (bitmap = 0; bitmap < taken->bitmap_count && taken->bitmaps[bitmap].xbox_index != xbox_index; bitmap++)
			;
		if (bitmap == taken->bitmap_count &&
			!shader_bitmap_take(map, xbox_index, &taken->bitmaps[taken->bitmap_count++]))
		{
			return FALSE;
		}
		taken->map_bitmaps[index] = (short)bitmap;
	}

	return TRUE;
}

/* the pixels of the shader's bitmaps read from the map they were taken from
(FALSE: not all of them) */
static boolean xbox_shader_pixels_read(
	struct xbox_map const *map,
	struct xbox_shader const *shader)
{
	struct xbox_map_piece pieces[MAXIMUM_SHADER_BITMAPS * MAXIMUM_BITMAPS];
	long count = 0;
	long bitmap, index;

	for (bitmap = 0; bitmap < shader->bitmap_count; bitmap++)
	{
		struct bitmap_group const *group = shader->bitmaps[bitmap].group;

		for (index = 0; index < group->bitmaps.count; index++)
		{
			struct bitmap_data const *data = (struct bitmap_data const *)group->bitmaps.address + index;

			pieces[count].offset = (unsigned long)data->pixels_offset;
			pieces[count].size = (unsigned long)data->pixels_size;
			pieces[count].bytes = shader->bitmaps[bitmap].pixels[index];
			count++;
		}
	}

	return xbox_map_pieces_read(map->name, pieces, count);
}

/* swap `swap_index`'s Xbox shader read from the first of its maps with it,
once a run: FALSE if none has it */
static boolean xbox_shader_read(
	short swap_index)
{
	struct xbox_shader_swap const *swap = &xbox_shader_swaps[swap_index];
	struct xbox_shader *shader = &xbox_shaders.shaders[swap_index];
	short map_index;

	if (shader->read)
		return !shader->failed;
	shader->read = TRUE;
	shader->failed = TRUE;
	for (map_index = 0; map_index < swap->xbox_map_count; map_index++)
	{
		struct xbox_map map;
		boolean taken;

		if (!xbox_map_read(swap->xbox_maps[map_index], &map))
			continue;
		if (xbox_tag_find(&map, 'sotr', swap->xbox_name) == NONE)
		{
			free(map.tags);
			continue;
		}
		taken = xbox_shader_take(&map, swap->xbox_name, shader) && xbox_shader_pixels_read(&map, shader);
		free(map.tags);
		if (taken)
		{
			shader->failed = FALSE;
			error(_error_silent, "xbox shaders: '%s' read from %s.map (%ld bitmaps)",
				swap->xbox_name, swap->xbox_maps[map_index], shader->bitmap_count);
			return TRUE;
		}
		xbox_shader_free(shader);
		error(_error_silent, "xbox shaders: %s.map's '%s' is damaged", swap->xbox_maps[map_index], swap->xbox_name);
		break;
	}
	error(_error_silent, "xbox shaders: none of the game's maps has '%s': Halo PC's is drawn", swap->xbox_name);

	return FALSE;
}

/* the map's tag of Halo PC's shader of swap `swap_index`, NONE if it has
none */
static long halo_pc_shader_find(
	short swap_index)
{
	char const *name = xbox_shader_swaps[swap_index].halo_pc_name;
	long shader_index = tag_loaded('schi', name);

	return shader_index != NONE ? shader_index : tag_loaded('scex', name);
}

/* ---------- public code */

void xbox_shaders_tags_loaded(
	void)
{
	struct cache_file_tag_instance *instances;
	long shader_indices[NUMBEROF(xbox_shader_swaps)];
	long count, index, added = 0, first_salt = 0, next;
	short swap_index;

	if (xbox_shaders.installed || !custom_edition_cache_tags_loaded())
		return;
	instances = cache_files_tag_instances(&count);
	if (!instances || count <= 0)
		return;
	/* the swaps this map has, their shaders read */
	for (swap_index = 0; swap_index < NUMBER_OF_XBOX_SHADER_SWAPS; swap_index++)
	{
		shader_indices[swap_index] = NONE;
		xbox_shaders.shaders[swap_index].installed = FALSE;
		if (config_boolean(xbox_shader_swaps[swap_index].setting))
		{
			long shader_index = halo_pc_shader_find(swap_index);

			if (shader_index != NONE && DATUM_INDEX_TO_ABSOLUTE_INDEX(shader_index) < count &&
				xbox_shader_read(swap_index))
			{
				shader_indices[swap_index] = shader_index;
				added += xbox_shaders.shaders[swap_index].bitmap_count;
			}
		}
	}
	for (swap_index = 0; swap_index < NUMBER_OF_XBOX_SHADER_SWAPS && shader_indices[swap_index] == NONE; swap_index++)
		;
	if (swap_index == NUMBER_OF_XBOX_SHADER_SWAPS || count + added > UNSIGNED_SHORT_MAX)
		return;
	xbox_shaders.instances = malloc((size_t)(count + added) * sizeof(*instances));
	if (!xbox_shaders.instances)
		return;
	memcpy(xbox_shaders.instances, instances, (size_t)count * sizeof(*instances));
	for (index = 0; index < count; index++)
	{
		long salt = (long)((unsigned long)instances[index].tag_index >> 16);

		if (instances[index].tag_index != NONE && salt > first_salt)
			first_salt = salt;
	}
	first_salt++;
	if (first_salt + added >= UNSIGNED_SHORT_MAX)
	{
		free(xbox_shaders.instances);
		xbox_shaders.instances = NULL;
		return;
	}
	next = 0;
	for (swap_index = 0; swap_index < NUMBER_OF_XBOX_SHADER_SWAPS; swap_index++)
	{
		struct xbox_shader *shader = &xbox_shaders.shaders[swap_index];
		struct tag_block *maps;

		if (shader_indices[swap_index] == NONE)
			continue;
		/* its bitmaps, after the map's tags (and the menus') */
		for (index = 0; index < shader->bitmap_count; index++, next++)
		{
			struct shader_bitmap *bitmap = &shader->bitmaps[index];
			struct cache_file_tag_instance *instance = &xbox_shaders.instances[count + next];
			long data_index;

			shader->handles[index] = ((first_salt + next) << 16) | (count + next);
			memset(instance, 0, sizeof(*instance));
			instance->group_tag = 'bitm';
			instance->parent_group_tags[0] = NONE;
			instance->parent_group_tags[1] = NONE;
			instance->tag_index = shader->handles[index];
			instance->name = bitmap->name;
			instance->base_address = bitmap->group;
			for (data_index = 0; data_index < bitmap->group->bitmaps.count; data_index++)
			{
				struct bitmap_data *data = (struct bitmap_data *)bitmap->group->bitmaps.address + data_index;

				data->tag_index = shader->handles[index];
				data->cache_block_index = NONE;
				data->hardware_format = NULL;
				data->base_address = NULL;
			}
		}
		/* the shader's maps name them (by this map's handles) */
		maps = (struct tag_block *)(shader->shader + 0x54);
		for (index = 0; index < maps->count; index++)
		{
			struct tag_reference *reference = (struct tag_reference *)((byte *)maps->address + index * GENERIC_MAP_BYTES + 0x1C);
			short bitmap = shader->map_bitmaps[index];

			reference->group_tag = 'bitm';
			reference->name = bitmap != NONE ? shader->bitmaps[bitmap].name : "";
			reference->name_length = (long)strlen(reference->name);
			reference->index = bitmap != NONE ? shader->handles[bitmap] : NONE;
		}
		/* and Halo PC's shader's tag is the Xbox's */
		{
			struct cache_file_tag_instance *instance =
				&xbox_shaders.instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(shader_indices[swap_index])];

			instance->group_tag = 'sotr';
			instance->base_address = shader->shader;
		}
		shader->installed = TRUE;
	}
	xbox_shaders.original_instances = instances;
	xbox_shaders.original_count = count;
	xbox_shaders.installed = TRUE;
	cache_files_set_tag_instances(xbox_shaders.instances, count + added);

	return;
}

void xbox_shaders_tags_unloaded(
	void)
{
	short swap_index;

	if (!xbox_shaders.installed)
		return;
	cache_files_set_tag_instances(xbox_shaders.original_instances, xbox_shaders.original_count);
	free(xbox_shaders.instances);
	xbox_shaders.instances = NULL;
	xbox_shaders.original_instances = NULL;
	xbox_shaders.installed = FALSE;
	for (swap_index = 0; swap_index < NUMBER_OF_XBOX_SHADER_SWAPS; swap_index++)
		xbox_shaders.shaders[swap_index].installed = FALSE;

	return;
}

boolean xbox_shaders_read(
	long tag_index,
	long offset,
	long size,
	void *buffer)
{
	short swap_index;

	if (!xbox_shaders.installed || tag_index == NONE)
		return FALSE;
	for (swap_index = 0; swap_index < NUMBER_OF_XBOX_SHADER_SWAPS; swap_index++)
	{
		struct xbox_shader const *shader = &xbox_shaders.shaders[swap_index];
		long bitmap_index;

		if (!shader->installed)
			continue;
		for (bitmap_index = 0; bitmap_index < shader->bitmap_count; bitmap_index++)
		{
			struct shader_bitmap const *bitmap = &shader->bitmaps[bitmap_index];
			long data_index;

			if (shader->handles[bitmap_index] != tag_index)
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
	}

	return FALSE;
}
