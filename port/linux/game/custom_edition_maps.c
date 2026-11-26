/*
CUSTOM_EDITION_MAPS.C

Lists Custom Edition maps for the menus (custom_edition_maps.h).

Whenever a map list opens, the custom_maps folder and then the Halo Custom
Edition install's maps folder are scanned for CE caches (.map; OpenSauce
caches are refused by the loader, so not listed). Multiplayer maps join the
level list after the Xbox levels, and the map lists' CUSTOM MULTIPLAYER;
Halo PC's own six (stock_maps) come first, and the map lists' MULTIPLAYER
shows them after the Xbox's maps (custom-387), with Halo PC's pictures
(its ui.map's, from the install) and descriptions;
campaign maps (solo scenarios) are the map lists' CUSTOM SINGLEPLAYER,
played alone or as network co-op. The stock campaign levels get display
indices here too, so the menus can show them the same way.

A map's level name is custom_maps\<name> (CUSTOM_EDITION_LEVEL_NAME_PREFIX),
which the cache loader reads from the custom maps folders alone: a map named
as one of the game's own levels (a30.map, bloodgulch.map) is listed and
played as itself. The game engine keeps 63 characters of a level name, so a
map whose name is longer than 51 is skipped. A map keeps its name: a network
game names it so, and says which version it is on by its hash (map_share.h).
A downloaded map of a name another version has is named <name>_<hash>.map;
the menus show it without the hash, with (2), (3) and so on after the first
of the name. A map a build of before named for its hash has its name given
back (map_share_map_found).

Each map can have, beside it in its folder:
- <name>.bmp, its picture (bmp_files.c; for a map named for its hash, a
  picture named without the hash also does). The middle is cropped to the
  menus' picture shape. It is read the first time it is drawn, and freed
  when the maps are rescanned. Without one, the unknown level's picture
  is shown.
- <name>.txt, its description, line by line (the Xbox descriptions use
  lines of about 20 characters).
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "tag_files/files.h"
#include "tag_files/tag_files.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmap_group.h"
#include "cache/cache_files.h"
#include "main/main.h"
#include "saved games/player_profile.h"
#include "text/unicode.h"
#include "bmp_files.h"
#include "custom_edition_cache.h"
#include "custom_edition_maps.h"
#include "map_share.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xtl.h>

/* Unmodified iOrange/bcdec, MIT option (see third_party/bcdec/LICENSE), as
mcc_bitmaps.c has it: Halo PC's map pictures are DXT1 */
#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "../../third_party/bcdec/bcdec.h"

/* ---------- constants */

/* Custom Edition campaign maps, for co-op */
#define MAXIMUM_CUSTOM_EDITION_CAMPAIGNS 1024
#define FIRST_CUSTOM_CAMPAIGN_DISPLAY_INDEX 0x6000
/* room for the level list's Xbox levels (ui_widget_event_handler_functions.c has 13) */
#define MAXIMUM_XBOX_LEVELS 16

/* the longest map file name whose level name
(CUSTOM_EDITION_LEVEL_NAME_PREFIX and it) the game engine keeps whole */
#define MAXIMUM_MAP_NAME_LENGTH \
	(CUSTOM_EDITION_MAXIMUM_LEVEL_NAME_LENGTH - (NUMBEROF(CUSTOM_EDITION_LEVEL_NAME_PREFIX) - 1))

/* Display indices for CE multiplayer maps, clear of every string and frame
index in the menus' tags (15 level names, 14 level pictures), and below the
custom campaigns' and ui_widget.c's spinner descriptions' (0x7000). */
#define FIRST_DISPLAY_INDEX 0x4000

/* The stock campaign levels, for co-op: their display indices, names, and
pictures (the campaign menu's, one frame per level in order). */
#define FIRST_CAMPAIGN_DISPLAY_INDEX 0x3000
#define CAMPAIGN_LEVEL_PICTURES_TAG_NAME "ui\\shell\\bitmaps\\sp_levels"
static wchar_t campaign_level_names[NUMBER_OF_SINGLE_PLAYER_LEVELS][32] =
{
	L"The Pillar of Autumn", L"Halo", L"The Truth and Reconciliation", L"The Silent Cartographer",
	L"Assault on the Control Room", L"343 Guilty Spark", L"The Library", L"Two Betrayals", L"Keyes", L"The Maw",
};
static wchar_t campaign_level_description[] = L"A campaign level, played co-op";

/* The level pictures tag, the picture shape (140x114 in the menus' 640x480;
the lobby's is 139x113), and the unknown level's frame
(ui_widget_game_data_input_functions.c). */
#define LEVEL_PICTURES_TAG_NAME "ui\\shell\\bitmaps\\mp_map_grafix"
#define LEVEL_PICTURE_SHAPE_WIDTH 140
#define LEVEL_PICTURE_SHAPE_HEIGHT 114
#define UNKNOWN_LEVEL_FRAME 13

#define PICTURE_EXTENSION ".bmp"
/* a 4K screenshot is 25 MB as a bmp file */
#define MAXIMUM_PICTURE_FILE_BYTES 0x04000000L
/* picture textures are square, about twice the menus' picture size */
#define PICTURE_TEXTURE_SIZE 256

#define DESCRIPTION_EXTENSION ".txt"
/* descriptions are a few short lines */
#define MAXIMUM_DESCRIPTION_FILE_BYTES 1024
#define MAXIMUM_DESCRIPTION_LENGTH 127

/* bitmaps.c's format of 32-bit color with alpha */
enum
{
	_bitmap_format_a8r8g8b8 = 11,
};

/* (the multiplayer maps', custom campaigns' and spinner descriptions'
display indices do not meet) */
typedef char verify_multiplayer_display_indices[
	FIRST_DISPLAY_INDEX + CUSTOM_EDITION_MAPS_MAXIMUM <= FIRST_CUSTOM_CAMPAIGN_DISPLAY_INDEX ? 1 : -1];
typedef char verify_campaign_display_indices[
	FIRST_CUSTOM_CAMPAIGN_DISPLAY_INDEX + MAXIMUM_CUSTOM_EDITION_CAMPAIGNS <= 0x7000 ? 1 : -1];

/* ---------- structures */

struct custom_edition_map
{
	/* file name without extension, and the folder it was found in */
	char name[MAXIMUM_MAP_NAME_LENGTH + 1];
	char folder[32];
	/* saved_game_file_remember_last_used_multiplayer_map writes
	MAXIMUM_FILENAME_LENGTH+1 characters of a level name */
	char level_name[MAXIMUM_FILENAME_LENGTH + 1];
	/* (and room for a version's number, " (2)") */
	wchar_t display_name[MAXIMUM_MAP_NAME_LENGTH + 8];
	wchar_t description[MAXIMUM_DESCRIPTION_LENGTH + 1];
	boolean picture_read;
	struct bitmap_data *picture;
};

struct custom_edition_maps_globals
{
	boolean looked_for;
	short map_count;
	/* the first of maps are Halo PC's own (stock_maps), this many, the
	map lists' MULTIPLAYER's after the Xbox's maps */
	short stock_count;
	struct custom_edition_map maps[CUSTOM_EDITION_MAPS_MAXIMUM];
	/* campaign maps, kept out of the level list */
	short campaign_count;
	struct custom_edition_map campaigns[MAXIMUM_CUSTOM_EDITION_CAMPAIGNS];
	/* the latest level list: the Xbox levels, then the CE maps */
	short xbox_level_count;
	char *levels[MAXIMUM_XBOX_LEVELS + CUSTOM_EDITION_MAPS_MAXIMUM];
};

/* ---------- globals */

static struct custom_edition_maps_globals custom_edition_maps_globals;

/* Halo PC's own multiplayer maps (not on the Xbox), with their menu names
from ui.map's mp_map_list and their descriptions from its map_data, in its
order: the first after the Xbox's 13 maps (the pictures of its
mp_map_grafix are in that order too, STOCK_MAP_FIRST_PICTURE on) */
#define STOCK_MAP_FIRST_PICTURE 13
static struct
{
	char const *file;
	wchar_t const *name;
	wchar_t const *description;
} const stock_maps[] =
{
	{ "icefields", L"Ice Fields", L"Slipping and\r\nSliding\r\n\r\n4-16 players\r\n\r\nSupports vehicles" },
	{ "deathisland", L"Death Island", L"Sand, Surf, and\r\nSpent Shells\r\n\r\n4-16 players\r\n\r\nSupports vehicles" },
	{ "dangercanyon", L"Danger Canyon",
		L"Don't Look Down...\r\nUnless You Fall\r\n\r\n4-16 players\r\n\r\nSupports vehicles" },
	{ "infinity", L"Infinity", L"I Imagined it\r\nWould Be Bigger\r\n\r\n4-16 players\r\n\r\nSupports vehicles" },
	{ "timberland", L"Timberland", L"An Enemy Behind\r\nEvery Tree\r\n\r\n4-16 players\r\n\r\nSupports vehicles" },
	{ "gephyrophobia", L"Gephyrophobia", L"Scary, huh?\r\n\r\n\r\n2-12 players\r\n\r\nSupports vehicles" },
};

/* Halo PC's menus' map pictures, in its install's ui.map: the tag, and the
part of each 256x128 picture the menus show (LEVEL_PICTURE_SHAPE, at its
corner) */
#define STOCK_MAP_PICTURES_TAG_NAME "ui\\shell\\bitmaps\\mp_map_grafix"
#define STOCK_MAP_UI_MAP CUSTOM_EDITION_INSTALL_MAP_DIRECTORY "ui.map"
#define STOCK_MAP_BITMAPS_MAP CUSTOM_EDITION_INSTALL_MAP_DIRECTORY "bitmaps.map"
/* a Halo PC (7) or Custom Edition (609) cache's tags are read at this
address; at most this much of them is read */
#define STOCK_MAP_TAG_BASE 0x40440000UL
#define STOCK_MAP_MAXIMUM_TAG_BYTES 0x04000000UL

/* for a map without a description file */
static wchar_t const default_description[] = L"Halo Custom\r\nEdition map";

/* ---------- private code */

static void custom_edition_maps_forget(
	void)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	short map_index;

	for (map_index = 0; map_index < globals->map_count; map_index++)
	{
		if (globals->maps[map_index].picture)
			bitmap_delete(globals->maps[map_index].picture);
	}
	for (map_index = 0; map_index < globals->campaign_count; map_index++)
	{
		if (globals->campaigns[map_index].picture)
			bitmap_delete(globals->campaigns[map_index].picture);
	}
	globals->map_count = 0;
	globals->stock_count = 0;
	globals->campaign_count = 0;

	return;
}

/* the map's index in stock_maps, or NONE (by its name without its hash) */
static short stock_map_index(
	char const *name)
{
	short length = map_share_name_base_length(name);
	short index;

	for (index = 0; index < NUMBEROF(stock_maps); index++)
	{
		if ((short)csstrlen(stock_maps[index].file) == length && !_strnicmp(name, stock_maps[index].file, length))
			return index;
	}
	return NONE;
}

/* the map's index in stock_maps when it is the map of that name (not a
version of it, named for its hash), or NONE */
static short stock_map_exact_index(
	char const *name)
{
	return map_share_name_hashed(name) ? NONE : stock_map_index(name);
}

/* A map's menu name: Halo PC's name for its own maps, otherwise the file
name (without a hash it ends in) with underscores as spaces and words
capitalized ("the_bay_of_pigs" becomes "The Bay Of Pigs"). */
static void display_name_make(
	char const *name,
	wchar_t *display_name)
{
	boolean word_start = TRUE;
	short index = stock_map_index(name);
	short length = map_share_name_base_length(name);

	if (index != NONE)
	{
		ustrcpy(display_name, stock_maps[index].name);
		return;
	}

	for (index = 0; index < length; index++)
	{
		unsigned char character = (unsigned char)name[index];

		if (character == '_')
		{
			character = ' ';
		}
		else if (word_start && character >= 'a' && character <= 'z')
		{
			character = (unsigned char)(character - 'a' + 'A');
		}
		word_start = character == ' ';
		display_name[index] = (wchar_t)character;
	}
	display_name[index] = 0;

	return;
}

/* Reads <name>.txt into map->description: line ends become \r\n (as in the
menus' strings), tabs become spaces, and non-ASCII characters become '?'.
A missing or empty file gives the default description. */
static void custom_edition_map_description_read(
	struct custom_edition_map *map)
{
	char path[MAXIMUM_FILENAME_LENGTH + 1];
	byte text[MAXIMUM_DESCRIPTION_FILE_BYTES];
	FILE *stream;
	long text_size = 0;
	long text_index = 0;
	short length = 0;

	csprintf(path, "%s%s%s", map->folder, map->name, DESCRIPTION_EXTENSION);
	stream = fopen(path, "rb");
	/* (a map named for its hash: its description named without it too) */
	if (!stream && map_share_name_hashed(map->name))
	{
		csprintf(path, "%s%.*s%s", map->folder, (int)map_share_name_base_length(map->name), map->name,
			DESCRIPTION_EXTENSION);
		stream = fopen(path, "rb");
	}
	if (stream)
	{
		text_size = (long)fread(text, 1, sizeof(text), stream);
		fclose(stream);
	}
	/* the byte order mark of UTF-8 text */
	if (text_size >= 3 && text[0] == 0xEF && text[1] == 0xBB && text[2] == 0xBF)
	{
		text_index = 3;
	}
	for (; text_index < text_size && length < MAXIMUM_DESCRIPTION_LENGTH; text_index++)
	{
		byte character = text[text_index];

		if (character == '\n')
		{
			if (length + 2 > MAXIMUM_DESCRIPTION_LENGTH)
			{
				break;
			}
			map->description[length++] = '\r';
			map->description[length++] = '\n';
		}
		else if (character == '\t')
		{
			map->description[length++] = ' ';
		}
		else if (character >= ' ' && character < 0x7F)
		{
			map->description[length++] = character;
		}
		else if (character >= 0xC0)
		{
			/* the lead byte of a multi-byte UTF-8 character */
			map->description[length++] = '?';
		}
	}
	while (length > 0 &&
		(map->description[length - 1] == ' ' ||
			map->description[length - 1] == '\r' ||
			map->description[length - 1] == '\n'))
	{
		length--;
	}
	map->description[length] = 0;

	if (!length && stock_map_exact_index(map->name) != NONE)
	{
		ustrncpy(map->description, stock_maps[stock_map_exact_index(map->name)].description, MAXIMUM_DESCRIPTION_LENGTH);
		map->description[MAXIMUM_DESCRIPTION_LENGTH] = 0;
	}
	else if (!length)
	{
		csmemcpy(map->description, default_description, sizeof(default_description));
	}

	return;
}

/* Adds the file `name`.`extension` from `folder` if it is a CE map that
isn't listed yet. */
static void custom_edition_map_add(
	char const *folder,
	char const *name,
	char const *extension)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	struct custom_edition_map *map;
	boolean campaign;
	short map_index;

	if (csstrcasecmp(extension, "map"))
	{
		return;
	}
	for (map_index = 0; map_index < globals->map_count; map_index++)
	{
		if (!csstrcasecmp(globals->maps[map_index].name, name))
		{
			return;
		}
	}
	for (map_index = 0; map_index < globals->campaign_count; map_index++)
	{
		if (!csstrcasecmp(globals->campaigns[map_index].name, name))
		{
			return;
		}
	}
	/* campaign maps go to co-op; anything that isn't multiplayer either is skipped */
	campaign = custom_edition_cache_campaign(name);
	if (!campaign && !custom_edition_cache_multiplayer(name))
	{
		return;
	}
	if (campaign)
	{
		if (globals->campaign_count == MAXIMUM_CUSTOM_EDITION_CAMPAIGNS || csstrlen(name) > MAXIMUM_MAP_NAME_LENGTH)
		{
			error(_error_silent, "custom edition: the campaign map '%s' is not listed (too many, or too long a name)",
				name);
			return;
		}
		map = &globals->campaigns[globals->campaign_count++];
	}
	else if (csstrlen(name) > MAXIMUM_MAP_NAME_LENGTH)
	{
		error(
			_error_silent,
			"custom edition: the map '%s' is not in the level list: its name is longer than %d characters",
			name,
			MAXIMUM_MAP_NAME_LENGTH);
		return;
	}
	else if (globals->map_count == CUSTOM_EDITION_MAPS_MAXIMUM)
	{
		error(
			_error_silent,
			"custom edition: the map '%s' is not in the level list, which holds %d of them",
			name,
			CUSTOM_EDITION_MAPS_MAXIMUM);
		return;
	}
	else
	{
		map = &globals->maps[globals->map_count++];
	}
	csmemset(map, 0, sizeof(*map));
	csstrcpy(map->name, name);
	csstrncpy(map->folder, folder, sizeof(map->folder) - 1);
	csprintf(map->level_name, "%s%s", CUSTOM_EDITION_LEVEL_NAME_PREFIX, name);
	display_name_make(name, map->display_name);
	custom_edition_map_description_read(map);
	/* (named for its hash by a build of before: its name given back,
	map_share_files.c) */
	map_share_map_found(folder, name);

	return;
}

static int custom_edition_map_compare(
	void const *first,
	void const *second)
{
	return (int)csstrcasecmp(
		((struct custom_edition_map const *)first)->name,
		((struct custom_edition_map const *)second)->name);
}

/* Versions of a map listed (its name the same but for the hash a version's
file name ends in, a download's beside another of its name): the first in
the order the maps are sorted by name (the one of the name alone, which
sorts before them) shown by its name, the others with (2), (3) and so on. */
static void custom_edition_maps_tell_versions_apart(
	struct custom_edition_map *maps,
	short count)
{
	short index;

	for (index = 0; index < count; index++)
	{
		struct custom_edition_map *map = &maps[index];
		short length = map_share_name_base_length(map->name);
		short number = 1;
		short other;

		if (!map_share_name_hashed(map->name))
			continue;
		for (other = 0; other < count; other++)
		{
			struct custom_edition_map const *version = &maps[other];

			if (other == index || map_share_name_base_length(version->name) != length ||
				_strnicmp(version->name, map->name, length))
			{
				continue;
			}
			if (other < index)
				number++;
		}
		if (number > 1)
		{
			char suffix[8];
			short end = 0;
			short character;

			while (map->display_name[end])
				end++;
			csprintf(suffix, " (%d)", (int)MIN(number, 999));
			for (character = 0; suffix[character] && end + 1 < NUMBEROF(map->display_name); character++)
				map->display_name[end++] = (wchar_t)suffix[character];
			map->display_name[end] = 0;
		}
	}
}

static void custom_edition_maps_look_for(
	void)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	char const *folders[] = { CUSTOM_EDITION_MAP_DIRECTORY, CUSTOM_EDITION_INSTALL_MAP_DIRECTORY };
	short folder_index;

	custom_edition_maps_forget();
	globals->looked_for = TRUE;
	if (!halo_custom_edition_tag_cache())
	{
		return;
	}

	/* the game's custom_maps first, so its copy wins */
	for (folder_index = 0; folder_index < NUMBEROF(folders); folder_index++)
	{
		char pattern[MAXIMUM_FILENAME_LENGTH + 1];
		WIN32_FIND_DATAA data;
		HANDLE find;

		/* (the install's only when there is one) */
		if (folder_index && !custom_edition_install_present())
			continue;
		/* A folder is listed through a handle of its own, never with
		find_files_start and find_files_next: those keep their place in one
		global, and as the game starts a thread of the menus' deletes the
		files of folders with them (perform_filesystem_initialization in
		ui_widget.c, directory_create_or_delete_contents). A listing here at
		the same time takes that one's place, and the thread then deletes
		the files of this folder: the maps. */
		csprintf(pattern, "%s*.*", folders[folder_index]);
		find = FindFirstFileA(pattern, &data);
		if (find == INVALID_HANDLE_VALUE)
			continue;
		do
		{
			char name[sizeof(data.cFileName) + 1];
			char *extension;

			if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			csmemcpy(name, data.cFileName, sizeof(data.cFileName));
			name[sizeof(data.cFileName)] = 0;
			extension = strrchr(name, '.');
			if (extension)
			{
				*extension++ = 0;
				custom_edition_map_add(folders[folder_index], name, extension);
			}
		} while (FindNextFileA(find, &data));
		CloseHandle(find);
	}
	qsort(globals->maps, globals->map_count, sizeof(globals->maps[0]), custom_edition_map_compare);
	qsort(globals->campaigns, globals->campaign_count, sizeof(globals->campaigns[0]), custom_edition_map_compare);
	custom_edition_maps_tell_versions_apart(globals->maps, globals->map_count);
	custom_edition_maps_tell_versions_apart(globals->campaigns, globals->campaign_count);
	/* (Halo PC's own first, in its menus' order: MULTIPLAYER's) */
	globals->stock_count = 0;
	{
		static struct custom_edition_map moved;
		short stock;
		short map_index;

		for (stock = 0; stock < NUMBEROF(stock_maps); stock++)
		{
			for (map_index = globals->stock_count; map_index < globals->map_count; map_index++)
			{
				if (stock_map_exact_index(globals->maps[map_index].name) == stock)
					break;
			}
			if (map_index == globals->map_count)
				continue;
			moved = globals->maps[map_index];
			memmove(&globals->maps[globals->stock_count + 1], &globals->maps[globals->stock_count],
				(map_index - globals->stock_count) * sizeof(globals->maps[0]));
			globals->maps[globals->stock_count++] = moved;
		}
	}
	error(_error_silent, "custom edition: %d multiplayer maps for the level list (%d of them Halo PC's own), %d campaign maps",
		globals->map_count, globals->stock_count, globals->campaign_count);

	return;
}

/* (stock_map_picture_read) bytes of a file at an offset; FALSE when it has
fewer */
static boolean stock_map_file_read(
	FILE *stream,
	unsigned long offset,
	void *buffer,
	unsigned long size)
{
	return fseek(stream, (long)offset, SEEK_SET) == 0 && fread(buffer, 1, size, stream) == size;
}

/* (stock_map_picture_read) the little-endian word or long at `offset` of
`bytes`, which has `size` of them; 0 past them */
static unsigned long stock_map_long(
	byte const *bytes,
	unsigned long size,
	unsigned long offset)
{
	return offset <= size && size - offset >= 4 ? (unsigned long)bytes[offset] | ((unsigned long)bytes[offset + 1] << 8) |
		((unsigned long)bytes[offset + 2] << 16) | ((unsigned long)bytes[offset + 3] << 24) : 0;
}

static unsigned long stock_map_word(
	byte const *bytes,
	unsigned long size,
	unsigned long offset)
{
	return offset <= size && size - offset >= 2 ? (unsigned long)bytes[offset] | ((unsigned long)bytes[offset + 1] << 8) : 0;
}

/* Halo PC's picture of one of its own maps (stock_maps' `stock`), from its
install's ui.map (mp_map_grafix, read as its tags lay it out: a bitmap's
pixels in ui.map or bitmaps.map, DXT1), as the menus' picture texture; NULL
when there is none (logged). Every offset and size is checked against what
was read: the files are the player's. */
static struct bitmap_data *stock_map_picture_read(
	short stock)
{
	FILE *ui = fopen(STOCK_MAP_UI_MAP, "rb");
	FILE *pixels_file = NULL;
	byte header[0x800];
	byte *tags = NULL;
	byte *pixels = NULL;
	byte *rgba = NULL;
	struct bitmap_data *bitmap = NULL;
	unsigned long tag_offset, tag_size, instances, count, index;
	unsigned long width = 0, height = 0, pixel_offset = 0, pixel_size = 0, flags = 0, format = 0;
	boolean found = FALSE;
	char const *failure = "it has no map pictures";

	if (!ui)
		return NULL;
	if (!stock_map_file_read(ui, 0, header, sizeof(header)) || stock_map_long(header, sizeof(header), 0) != 'head' ||
		(stock_map_long(header, sizeof(header), 4) != 7 && stock_map_long(header, sizeof(header), 4) != 609))
	{
		failure = "it is not a Halo PC map";
		goto done;
	}
	tag_offset = stock_map_long(header, sizeof(header), 0x10);
	tag_size = stock_map_long(header, sizeof(header), 0x14);
	if (tag_size < 0x28 || tag_size > STOCK_MAP_MAXIMUM_TAG_BYTES || !(tags = malloc(tag_size)) ||
		!stock_map_file_read(ui, tag_offset, tags, tag_size))
	{
		failure = "its tags could not be read";
		goto done;
	}
	instances = stock_map_long(tags, tag_size, 0) - STOCK_MAP_TAG_BASE;
	count = stock_map_long(tags, tag_size, 0x0C);
	for (index = 0; index < count && !found; index++)
	{
		unsigned long entry = instances + index * 0x20;
		unsigned long name, data, bitmaps, bitmap_count, element, character;

		if (entry >= tag_size || tag_size - entry < 0x20)
			break;
		if (stock_map_long(tags, tag_size, entry) != 'bitm')
			continue;
		name = stock_map_long(tags, tag_size, entry + 0x10) - STOCK_MAP_TAG_BASE;
		for (character = 0; name < tag_size && character < tag_size - name && STOCK_MAP_PICTURES_TAG_NAME[character] &&
			(char)tags[name + character] == STOCK_MAP_PICTURES_TAG_NAME[character]; character++)
			;
		if (STOCK_MAP_PICTURES_TAG_NAME[character] || name + character >= tag_size || tags[name + character])
			continue;
		found = TRUE;
		data = stock_map_long(tags, tag_size, entry + 0x14) - STOCK_MAP_TAG_BASE;
		bitmap_count = stock_map_long(tags, tag_size, data + 0x60);
		bitmaps = stock_map_long(tags, tag_size, data + 0x64) - STOCK_MAP_TAG_BASE;
		if (data >= tag_size || (unsigned long)(STOCK_MAP_FIRST_PICTURE + stock) >= bitmap_count)
			break;
		element = bitmaps + (unsigned long)(STOCK_MAP_FIRST_PICTURE + stock) * 0x30;
		if (element >= tag_size || tag_size - element < 0x30)
			break;
		width = stock_map_word(tags, tag_size, element + 4);
		height = stock_map_word(tags, tag_size, element + 6);
		format = stock_map_word(tags, tag_size, element + 0x0C);
		flags = stock_map_word(tags, tag_size, element + 0x0E);
		pixel_offset = stock_map_long(tags, tag_size, element + 0x18);
		pixel_size = stock_map_long(tags, tag_size, element + 0x1C);
	}
	if (!width)
		goto done;
	/* (DXT1, at least the menus' picture, whole blocks) */
	if (format != 14 || width < LEVEL_PICTURE_SHAPE_WIDTH || height < LEVEL_PICTURE_SHAPE_HEIGHT || width > 1024 ||
		height > 1024 || (width & 3) || (height & 3) || pixel_size < width * height / 2)
	{
		failure = "its picture is not one this reads";
		goto done;
	}
	pixels_file = (flags & 0x100) ? fopen(STOCK_MAP_BITMAPS_MAP, "rb") : ui;
	pixels = malloc(width * height / 2);
	rgba = malloc(width * height * 4);
	if (!pixels_file || !pixels || !rgba || !stock_map_file_read(pixels_file, pixel_offset, pixels, width * height / 2))
	{
		failure = "its picture's pixels could not be read";
		goto done;
	}
	{
		unsigned long block_x, block_y;
		byte const *block = pixels;

		for (block_y = 0; block_y < height; block_y += 4)
		{
			for (block_x = 0; block_x < width; block_x += 4, block += 8)
				bcdec_bc1(block, rgba + (block_y * width + block_x) * 4, (int)(width * 4));
		}
	}
	bitmap = bitmap_2d_new(PICTURE_TEXTURE_SIZE, PICTURE_TEXTURE_SIZE, 0, _bitmap_format_a8r8g8b8);
	if (bitmap && bitmap->base_address)
	{
		uint32_t *texels = XBOX_POINTER(uint32_t, bitmap->base_address);
		unsigned long x, y;

		/* (the picture's corner, the menus' shape, over the whole texture:
		each texel from the four nearest pixels) */
		for (y = 0; y < PICTURE_TEXTURE_SIZE; y++)
		{
			real source_y = ((real)y + 0.5f) * LEVEL_PICTURE_SHAPE_HEIGHT / PICTURE_TEXTURE_SIZE - 0.5f;
			long y0 = (long)source_y;
			real fy = source_y - (real)y0;
			long y1;

			if (source_y < 0.0f)
			{
				y0 = 0;
				fy = 0.0f;
			}
			y1 = MIN(y0 + 1, LEVEL_PICTURE_SHAPE_HEIGHT - 1);
			for (x = 0; x < PICTURE_TEXTURE_SIZE; x++)
			{
				real source_x = ((real)x + 0.5f) * LEVEL_PICTURE_SHAPE_WIDTH / PICTURE_TEXTURE_SIZE - 0.5f;
				long x0 = (long)source_x;
				real fx = source_x - (real)x0;
				long x1;
				uint32_t texel = 0xFF000000u;
				short channel;

				if (source_x < 0.0f)
				{
					x0 = 0;
					fx = 0.0f;
				}
				x1 = MIN(x0 + 1, LEVEL_PICTURE_SHAPE_WIDTH - 1);
				for (channel = 0; channel < 3; channel++)
				{
					real top = rgba[(y0 * width + x0) * 4 + channel] * (1.0f - fx) + rgba[(y0 * width + x1) * 4 + channel] * fx;
					real bottom = rgba[(y1 * width + x0) * 4 + channel] * (1.0f - fx) + rgba[(y1 * width + x1) * 4 + channel] * fx;
					long value = (long)(top * (1.0f - fy) + bottom * fy + 0.5f);

					/* (rgba is red, green, blue; the texel red at 16) */
					texel |= (uint32_t)PIN(value, 0, 255) << (16 - 8 * channel);
				}
				texels[y * PICTURE_TEXTURE_SIZE + x] = texel;
			}
		}
		bitmap_rebuild(bitmap);
	}
	if (bitmap && !bitmap->hardware_format)
	{
		bitmap_delete(bitmap);
		bitmap = NULL;
	}
	failure = NULL;

done:
	if (failure)
		error(_error_silent, "custom edition: Halo PC's picture of %s is not shown: %s (%s)", stock_maps[stock].file,
			failure, STOCK_MAP_UI_MAP);
	if (pixels_file && pixels_file != ui)
		fclose(pixels_file);
	fclose(ui);
	free(tags);
	free(pixels);
	free(rgba);
	return bitmap;
}

/* Loads the map's picture as a texture. Returns NULL if it has none, or if
it can't be shown (logged). */
static struct bitmap_data *custom_edition_map_picture_read(
	struct custom_edition_map const *map)
{
	char path[MAXIMUM_FILENAME_LENGTH + 1];
	struct bmp_file_picture picture;
	enum bmp_file_status status;
	struct bitmap_data *bitmap = NULL;
	uint8_t *file = NULL;
	FILE *stream;
	long size = 0;

	csprintf(path, "%s%s%s", map->folder, map->name, PICTURE_EXTENSION);
	stream = fopen(path, "rb");
	/* (a map named for its hash: its picture named without it too) */
	if (!stream && map_share_name_hashed(map->name))
	{
		csprintf(path, "%s%.*s%s", map->folder, (int)map_share_name_base_length(map->name), map->name,
			PICTURE_EXTENSION);
		stream = fopen(path, "rb");
	}
	if (!stream)
	{
		/* (one of Halo PC's own: Halo PC's picture of it) */
		return stock_map_exact_index(map->name) != NONE ? stock_map_picture_read(stock_map_exact_index(map->name)) : NULL;
	}
	if (fseek(stream, 0, SEEK_END) == 0 &&
		(size = ftell(stream)) >= 0 &&
		size <= MAXIMUM_PICTURE_FILE_BYTES &&
		fseek(stream, 0, SEEK_SET) == 0)
	{
		/* +1 so an empty file doesn't look like a failed allocation */
		file = malloc((size_t)size + 1);
		if (file && fread(file, 1, (size_t)size, stream) != (size_t)size)
		{
			free(file);
			file = NULL;
		}
	}
	fclose(stream);
	if (!file)
	{
		error(_error_silent, "custom edition: the picture '%s' could not be read", path);
		return NULL;
	}

	status = bmp_file_open(file, (uint32_t)size, &picture);
	if (status == _bmp_file_status_ok)
	{
		bitmap = bitmap_2d_new(PICTURE_TEXTURE_SIZE, PICTURE_TEXTURE_SIZE, 0, _bitmap_format_a8r8g8b8);
		if (bitmap && bitmap->base_address)
		{
			bmp_file_fit(
				file,
				&picture,
				LEVEL_PICTURE_SHAPE_WIDTH,
				LEVEL_PICTURE_SHAPE_HEIGHT,
				PICTURE_TEXTURE_SIZE,
				PICTURE_TEXTURE_SIZE,
				XBOX_POINTER(uint32_t, bitmap->base_address));
			bitmap_rebuild(bitmap);
		}
		if (bitmap && !bitmap->hardware_format)
		{
			bitmap_delete(bitmap);
			bitmap = NULL;
		}
	}
	else
	{
		error(_error_silent, "custom edition: the picture '%s' cannot be shown: %s", path, bmp_file_status_describe(status));
	}
	free(file);

	return bitmap;
}

/* the CE map with this display index, or NULL */
static struct custom_edition_map *custom_edition_map_get(
	short display_index)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	short map_index = display_index - FIRST_DISPLAY_INDEX;
	short campaign_index = display_index - FIRST_CUSTOM_CAMPAIGN_DISPLAY_INDEX;

	if (campaign_index >= 0 && campaign_index < globals->campaign_count)
		return &globals->campaigns[campaign_index];
	return map_index >= 0 && map_index < globals->map_count ? &globals->maps[map_index] : NULL;
}

/* the stock campaign level with this display index, or NONE */
static short campaign_level_get(
	short display_index)
{
	short level = display_index - FIRST_CAMPAIGN_DISPLAY_INDEX;

	return level >= 0 && level < NUMBER_OF_SINGLE_PLAYER_LEVELS ? level : NONE;
}

/* the stock campaign level a level name refers to (levels\a10\a10 is the
first), or NONE */
static short campaign_level_from_name(
	char const *level_name)
{
	char const *name = tag_name_strip_path(level_name);
	short level;

	for (level = 0; level < NUMBER_OF_SINGLE_PLAYER_LEVELS; level++)
	{
		if (!csstrcasecmp(tag_name_strip_path(main_get_solo_level_name(level)), name))
			return level;
	}

	return NONE;
}

/* ---------- public code */

char **custom_edition_maps_level_list(
	char **xbox_levels,
	short xbox_level_count,
	short *level_count)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	short level_index;
	short map_index;

	globals->xbox_level_count = MIN(xbox_level_count, MAXIMUM_XBOX_LEVELS);
	for (level_index = 0; level_index < globals->xbox_level_count; level_index++)
	{
		globals->levels[level_index] = xbox_levels[level_index];
	}
	/* (the menus ask for the list every tick, so the folders are scanned
	once, and again when a map list opens: custom_edition_maps_look_again) */
	if (!globals->looked_for)
	{
		custom_edition_maps_look_for();
	}
	for (map_index = 0; map_index < globals->map_count; map_index++)
	{
		globals->levels[globals->xbox_level_count + map_index] = globals->maps[map_index].level_name;
	}
	*level_count = globals->xbox_level_count + globals->map_count;

	return globals->levels;
}

void custom_edition_maps_look_again(
	void)
{
	custom_edition_maps_globals.looked_for = FALSE;
	/* (a network game's map's file, found again) */
	custom_edition_map_paths_forget();

	return;
}

short custom_edition_maps_level_display_index(
	short level_index)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	short map_index = level_index - globals->xbox_level_count;

	return map_index >= 0 && map_index < globals->map_count ? FIRST_DISPLAY_INDEX + map_index : level_index;
}

short custom_edition_maps_display_index(
	char const *level_name)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;
	char const *name = tag_name_strip_path(level_name);
	char path[MAP_SHARE_PATH_SIZE];
	short map_index;

	/* (only a custom_maps\ name is a CE map's; any other is the game's own,
	a stock campaign level or none of these) */
	if (!custom_edition_level_name(level_name))
	{
		short level = campaign_level_from_name(level_name);

		return level != NONE ? FIRST_CAMPAIGN_DISPLAY_INDEX + level : NONE;
	}
	if (!globals->looked_for)
	{
		custom_edition_maps_look_for();
	}
	/* (the map by its file: a network game's of its version, which may be
	one of another name, <name>_<hash>.map, shown with its number) */
	if (custom_edition_map_file_path(level_name, path))
	{
		char const *file_name = tag_name_strip_path(path);
		short length = (short)csstrlen(file_name) - (short)(sizeof(".map") - 1);

		for (map_index = 0; length > 0 && map_index < globals->map_count; map_index++)
		{
			if ((short)csstrlen(globals->maps[map_index].name) == length &&
				!_strnicmp(globals->maps[map_index].name, file_name, length))
			{
				return FIRST_DISPLAY_INDEX + map_index;
			}
		}
		for (map_index = 0; length > 0 && map_index < globals->campaign_count; map_index++)
		{
			if ((short)csstrlen(globals->campaigns[map_index].name) == length &&
				!_strnicmp(globals->campaigns[map_index].name, file_name, length))
			{
				return FIRST_CUSTOM_CAMPAIGN_DISPLAY_INDEX + map_index;
			}
		}
	}
	for (map_index = 0; map_index < globals->map_count; map_index++)
	{
		if (!csstrcasecmp(globals->maps[map_index].name, name))
		{
			return FIRST_DISPLAY_INDEX + map_index;
		}
	}
	for (map_index = 0; map_index < globals->campaign_count; map_index++)
	{
		if (!csstrcasecmp(globals->campaigns[map_index].name, name))
		{
			return FIRST_CUSTOM_CAMPAIGN_DISPLAY_INDEX + map_index;
		}
	}

	return NONE;
}

boolean custom_edition_maps_campaign(
	short display_index)
{
	return campaign_level_get(display_index) != NONE ||
		(display_index >= FIRST_CUSTOM_CAMPAIGN_DISPLAY_INDEX && custom_edition_map_get(display_index));
}

short custom_edition_maps_campaign_level(
	short display_index)
{
	return campaign_level_get(display_index);
}

boolean custom_edition_maps_level_campaign(
	char const *level_name)
{
	return level_name && custom_edition_maps_campaign(custom_edition_maps_display_index(level_name));
}

short custom_edition_maps_count(
	boolean campaign)
{
	struct custom_edition_maps_globals *globals = &custom_edition_maps_globals;

	if (!globals->looked_for)
	{
		custom_edition_maps_look_for();
	}

	return campaign ? globals->campaign_count : globals->map_count - globals->stock_count;
}

short custom_edition_maps_display_index_of(
	boolean campaign,
	short index)
{
	return index < 0 || index >= custom_edition_maps_count(campaign) ? NONE :
		(campaign ? FIRST_CUSTOM_CAMPAIGN_DISPLAY_INDEX : FIRST_DISPLAY_INDEX + custom_edition_maps_globals.stock_count) +
		index;
}

char const *custom_edition_maps_level_name(
	short display_index)
{
	struct custom_edition_map *map = custom_edition_map_get(display_index);
	short level = campaign_level_get(display_index);

	if (level != NONE)
	{
		return main_get_solo_level_name(level);
	}

	return map ? map->level_name : NULL;
}

boolean custom_edition_maps_stock(
	short display_index)
{
	struct custom_edition_map *map = custom_edition_map_get(display_index);

	return map && stock_map_index(map->name) != NONE;
}

boolean custom_edition_maps_stock_file(
	char const *path)
{
	char name[MAXIMUM_MAP_NAME_LENGTH + 1];
	char const *file = path;
	char const *character;
	size_t length;

	if (!path)
		return FALSE;
	for (character = path; *character; character++)
	{
		if (*character == '\\' || *character == '/')
			file = character + 1;
	}
	length = strcspn(file, ".");
	if (!length || length > MAXIMUM_MAP_NAME_LENGTH)
		return FALSE;
	memcpy(name, file, length);
	name[length] = 0;

	return stock_map_exact_index(name) != NONE;
}

wchar_t *custom_edition_maps_name(
	short display_index)
{
	struct custom_edition_map *map = custom_edition_map_get(display_index);
	short level = campaign_level_get(display_index);

	if (level != NONE)
	{
		return campaign_level_names[level];
	}

	return map ? map->display_name : NULL;
}

wchar_t *custom_edition_maps_description(
	short display_index)
{
	struct custom_edition_map *map = custom_edition_map_get(display_index);

	if (campaign_level_get(display_index) != NONE)
	{
		return campaign_level_description;
	}

	return map ? map->description : NULL;
}

struct bitmap_data *custom_edition_maps_picture(
	long bitmap_tag_index,
	short *frame_index)
{
	struct custom_edition_map *map;
	short level = campaign_level_get(*frame_index);

	if (level != NONE &&
		bitmap_tag_index != NONE &&
		!csstrcasecmp(tag_get_name(bitmap_tag_index), LEVEL_PICTURES_TAG_NAME))
	{
		long pictures = tag_loaded('bitm', CAMPAIGN_LEVEL_PICTURES_TAG_NAME);

		*frame_index = UNKNOWN_LEVEL_FRAME;
		return pictures != NONE ? bitmap_group_get_bitmap_from_sequence(pictures, 0, level) : NULL;
	}
	if (*frame_index < FIRST_DISPLAY_INDEX ||
		bitmap_tag_index == NONE ||
		csstrcasecmp(tag_get_name(bitmap_tag_index), LEVEL_PICTURES_TAG_NAME))
	{
		return NULL;
	}

	map = custom_edition_map_get(*frame_index);
	*frame_index = UNKNOWN_LEVEL_FRAME;
	if (!map)
	{
		return NULL;
	}
	if (!map->picture_read)
	{
		map->picture = custom_edition_map_picture_read(map);
		map->picture_read = TRUE;
	}

	return map->picture;
}
