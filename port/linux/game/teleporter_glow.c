/*
TELEPORTER_GLOW.C

The green glow on the floor at the teleporters of Halo PC's own maps
(custom-387).

On the Xbox's maps the floor at a teleporter's foot shines green: Bungie's
tools worked the teleporter field's light into the level's lightmaps, a pool
of green before and around each one (Beaver Creek's, Blood Gulch's). Halo
PC's six maps of its own (custom_edition_maps.c's stock_maps: Danger Canyon,
Death Island and the others), lit by Gearbox, have little or none of it:
their teleporters stand on unlit grass (Danger Canyon) or floors (Death
Island's rooms), or faintly lit ones (Gephyrophobia, Infinity), and the
Xbox's field they now show (xbox_teleporters.c) lights nothing.

On those maps, each teleporter's entrance (a netgame flag of the teleporter
sources, the scenario's netgame_flags: a two-way teleporter has one at each
end; flags nearer than TELEPORTER_GLOW_MERGE_DISTANCE are one) is given a
green light just over the floor (TELEPORTER_GLOW_HEIGHT), reaching
TELEPORTER_GLOW_RADIUS: an ordinary dynamic light of the game's, drawn with a
light definition the map already has (object_lights.c's
light_port_glow_new_at, as shield_glow.c's glows are), so it lights the
floor, the frame and whoever stands there, as the Xbox's lightmaps do
around theirs. It does not move, and is kept each frame as the lights are
gathered (lights_preprocess_scene), made again if the lights were. Only the
drawing changes, nothing played or sent between machines.
display.teleporter_glow turns it off.
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "math/real_math.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "physics/collisions.h"
#include "tag_files/tag_groups.h"
#include "custom_edition_cache.h"
#include "custom_edition_maps.h"

#include <math.h>

/* ---------- constants */

enum
{
	MAXIMUM_TELEPORTER_GLOWS = 16
};

/* how far over the flag (on the floor) the light is, and how far it reaches
(world units) */
#define TELEPORTER_GLOW_HEIGHT 0.4f
#define TELEPORTER_GLOW_RADIUS 3.0f
/* flags this near each other are one teleporter's (world units) */
#define TELEPORTER_GLOW_MERGE_DISTANCE 1.5f
/* how far around the light the level's surfaces push it away (world units),
looked at in TELEPORTER_GLOW_DIRECTIONS directions, and the most they push it */
#define TELEPORTER_GLOW_PROBE_DISTANCE 3.0f
#define TELEPORTER_GLOW_DIRECTIONS 16
#define TELEPORTER_GLOW_MAXIMUM_PUSH 1.2f

/* ---------- globals */

static struct
{
	/* whether this map's teleporters were looked for */
	boolean found;
	short glow_count;
	real_point3d positions[MAXIMUM_TELEPORTER_GLOWS];
	long light_indices[MAXIMUM_TELEPORTER_GLOWS];
} teleporter_glow;

/* the teleporter field's green, at the light's strength */
static real_rgb_color const teleporter_glow_color = { 0.15f, 1.0f, 0.2f };

/* ---------- prototypes */

int config_boolean(char const *name);
unsigned long config_changes(void);

/* object_lights.c (port) */
long light_port_glow_new_at(real_point3d const *position, real_rgb_color const *color, real radius);
boolean light_port_glow_set(long light_index, long object_index, real_rgb_color const *color, real radius);

/* ---------- private code */

/* the light at `position` moved away from the level's surfaces around it
(the teleporter's frame, the cliff or wall behind it), toward the open side
its entrance faces, where the floor the Xbox's lightmaps light is: pushed
from each direction as much as a surface closes it, at most
TELEPORTER_GLOW_MAXIMUM_PUSH, and never through one */
static void teleporter_glow_place(
	real_point3d *position)
{
	unsigned long flags = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit);
	struct collision_result collision;
	real_vector3d push = { 0.f, 0.f, 0.f };
	real length;
	short direction;

	for (direction = 0; direction < TELEPORTER_GLOW_DIRECTIONS; direction++)
	{
		real angle = 2.f * _pi * (real)direction / (real)TELEPORTER_GLOW_DIRECTIONS;
		real_vector3d ray;
		real closed = 0.f;

		ray.i = (real)cos(angle) * TELEPORTER_GLOW_PROBE_DISTANCE;
		ray.j = (real)sin(angle) * TELEPORTER_GLOW_PROBE_DISTANCE;
		ray.k = 0.f;
		if (collision_test_vector(flags, position, &ray, NONE, &collision))
			closed = 1.f - collision.t;
		push.i -= (real)cos(angle) * closed;
		push.j -= (real)sin(angle) * closed;
	}
	length = square_root(push.i * push.i + push.j * push.j);
	if (length <= 0.f)
		return;
	/* (a sixth of the directions closed outright is a full push) */
	length = MIN(TELEPORTER_GLOW_MAXIMUM_PUSH, TELEPORTER_GLOW_MAXIMUM_PUSH * length /
		((real)TELEPORTER_GLOW_DIRECTIONS / 6.f)) / length;
	push.i *= length;
	push.j *= length;
	if (collision_test_vector(flags, position, &push, NONE, &collision))
	{
		push.i *= collision.t * 0.5f;
		push.j *= collision.t * 0.5f;
	}
	position->x += push.i;
	position->y += push.j;

	return;
}

/* the map's teleporters' entrances, when it is one of Halo PC's own */
static void teleporter_glow_find(
	void)
{
	struct scenario *scenario = global_scenario_get();
	long index;

	teleporter_glow.found = TRUE;
	teleporter_glow.glow_count = 0;
	if (!scenario || !custom_edition_maps_stock_file(custom_edition_cache_loaded_path()))
		return;
	for (index = 0; index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag const *flag =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);
		real_point3d position;
		short glow;

		if (!flag || flag->type != _netgame_flag_teleporter_source)
			continue;
		position = flag->position;
		position.z += TELEPORTER_GLOW_HEIGHT;
		for (glow = 0; glow < teleporter_glow.glow_count; glow++)
		{
			if (distance_squared3d(&teleporter_glow.positions[glow], &position) <
				TELEPORTER_GLOW_MERGE_DISTANCE * TELEPORTER_GLOW_MERGE_DISTANCE)
			{
				break;
			}
		}
		if (glow < teleporter_glow.glow_count || teleporter_glow.glow_count >= MAXIMUM_TELEPORTER_GLOWS)
			continue;
		teleporter_glow.positions[teleporter_glow.glow_count] = position;
		teleporter_glow_place(&teleporter_glow.positions[teleporter_glow.glow_count]);
		teleporter_glow.light_indices[teleporter_glow.glow_count] = NONE;
		teleporter_glow.glow_count++;
	}
	error(_error_silent, "teleporter glow: %d teleporters lit", (int)teleporter_glow.glow_count);

	return;
}

/* ---------- public code */

/* a new map's lights (lights_initialize_for_new_map): its teleporters looked
for again, the old map's lights gone with it */
void teleporter_glow_reset(
	void)
{
	teleporter_glow.found = FALSE;
	teleporter_glow.glow_count = 0;

	return;
}

/* each frame, before the lights are gathered (lights_preprocess_scene) */
void teleporter_glow_update(
	void)
{
	static unsigned long changes = (unsigned long)-1;
	static boolean enabled = FALSE;
	short glow;

	if (changes != config_changes())
	{
		changes = config_changes();
		enabled = config_boolean("display.teleporter_glow") != 0;
	}

	if (!teleporter_glow.found)
		teleporter_glow_find();
	for (glow = 0; glow < teleporter_glow.glow_count; glow++)
	{
		long *light_index = &teleporter_glow.light_indices[glow];

		/* (turned off: dark, gone a moment after, as the game's passing
		lights go) */
		if (!enabled)
		{
			if (*light_index != NONE)
				light_port_glow_set(*light_index, NONE, global_real_rgb_black, 0.f);
			*light_index = NONE;
			continue;
		}
		if (*light_index != NONE &&
			!light_port_glow_set(*light_index, NONE, &teleporter_glow_color, TELEPORTER_GLOW_RADIUS))
		{
			*light_index = NONE;
		}
		if (*light_index == NONE)
			*light_index = light_port_glow_new_at(&teleporter_glow.positions[glow], &teleporter_glow_color,
				TELEPORTER_GLOW_RADIUS);
	}

	return;
}
