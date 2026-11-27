/*
VEHICLE_CAMERA.C

The view getting into and out of a seat, drawn around the player's own body
(custom-387).

Getting into a seat whose camera follows the vehicle, the director moves the
view from the player's eyes to behind the vehicle over a second, and back
over a second on getting out (director_choose_game_perspective,
director_update). The view goes there in a line: out through the back of
the player's head, which is the third person's, drawn, from the move's
first frame. For a quarter of a second the picture is the inside of the
player's armor. Coming back, the back of the helmet fills the picture, and
with a fifth of the move left the director puts the view in the eyes at
once: a jump of a quarter of a world unit, the body gone and the
first-person weapon there in the same frame.

With display.vehicle_camera, the view drawn goes over the player's head
instead. Near the body (across, from its bounding sphere's center), it is
kept over the body's center by at least 1.1 bounding radii (0.21 world
units over the Master Chief's eyes, clear of his bounding sphere, whose
center is 0.25 under them), so that the body is under the picture as the
view rises out of it or comes down into it. Where the game's view is
higher it is the game's: the two meet in a curve, not a corner. Getting
in, the body climbs into the seat, away from the eyes the view leaves, and
the view rises over it as it goes (dipping by 0.03 world units at most as
the seated body rises under it, at Blood Gulch's warthog). The lift rises
over the move's first sixth from the eyes, is gone by its end at the other
camera, and stops under a surface over the view.

The rest of a move back that the director jumps is drawn as a glide into
the eyes, over the time the move had left, slowing to a stop: no faster to
begin with than the view came. While the view drawn is inside the body's
bounding sphere, the body is not drawn, nor what it carries
(render_objects.c).

Only the picture changes. The director's and the observer's camera, which
aim assistance and the sounds take, are the game's.
*/

/* ---------- headers */

#include "cseries.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "camera/director.h"
#include "camera/observer.h"
#include "game/game.h"
#include "game/players.h"
#include "physics/collisions.h"
#include "units/units.h"

#include "vehicle_camera.h"

/* ---------- constants */

/* how high over the body's center the view is kept, how gently the game's
view higher than that takes over (the curve's size), and how near the
body's center (horizontally) it is kept wholly and begins to be, each of the
body's bounding radius */
#define VEHICLE_CAMERA_HEIGHT 1.1f
#define VEHICLE_CAMERA_SOFTNESS 0.25f
#define VEHICLE_CAMERA_LIFT_WHOLE 3.0f
#define VEHICLE_CAMERA_LIFT_BEGIN 6.0f
/* the part of the move, from the eyes, the lift rises over, and the part
from which it settles to none at the move's other end */
#define VEHICLE_CAMERA_LIFT_RISE 0.15f
#define VEHICLE_CAMERA_LIFT_SETTLE 0.85f
/* how near a surface over it the lifted view comes (world units) */
#define VEHICLE_CAMERA_CEILING_GAP 0.05f
/* a jump into the eyes longer than this is not glided (world units) */
#define VEHICLE_CAMERA_GLIDE_LONGEST 1.5f
/* the shortest glide (seconds) */
#define VEHICLE_CAMERA_GLIDE_SHORTEST 0.05f

/* ---------- globals */

static struct vehicle_camera
{
	/* the view drawn the frame before, and when (the game's ticks with the
	frame's part of one); whether it was moving back to first person then,
	and the part of the move left */
	boolean drawn_valid;
	real_point3d drawn_position;
	real drawn_time;
	boolean returning;
	real returning_remaining;
	/* the rest of a move back the director jumped: the view drawn offset
	from the eyes, by less as it goes, over that many ticks from then (the
	game's ticks with the frame's part of one) */
	boolean gliding;
	real_vector3d glide_offset;
	real glide_start;
	real glide_ticks;
	/* the body the view drawn is inside of, NONE for none */
	long hidden_unit_index;
	struct observer_result view;
} vehicle_cameras[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];

/* ---------- prototypes */

int config_boolean(char const *name);
unsigned long config_changes(void);

/* ---------- private code */

static boolean vehicle_camera_enabled(
	void)
{
	static boolean enabled;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.vehicle_camera") != 0;
	}

	return enabled;
}

static real smooth_step(
	real edge0,
	real edge1,
	real x)
{
	real t = (x - edge0) / (edge1 - edge0);

	t = PIN(t, 0.f, 1.f);

	return t * t * (3.f - 2.f * t);
}

/* `position` lifted over a body of bounding sphere `center` and `radius`
(VEHICLE_CAMERA_HEIGHT over its center), as near it as it is, once the move
has gone `moved` (of 1) from the eyes; not to within
VEHICLE_CAMERA_CEILING_GAP of a surface over it */
static void vehicle_camera_lift(
	real_point3d *position,
	real_point3d const *center,
	real radius,
	real moved)
{
	real dx = position->x - center->x;
	real dy = position->y - center->y;
	real across = square_root(dx * dx + dy * dy);
	real short_of = VEHICLE_CAMERA_HEIGHT * radius - (position->z - center->z);
	real softness = VEHICLE_CAMERA_SOFTNESS * radius;
	/* (as much as the view is short of the height, and a little where it is
	nearly there or over it: the curve) */
	real lift = 0.5f * (short_of + square_root(short_of * short_of + softness * softness)) *
		smooth_step(0.f, VEHICLE_CAMERA_LIFT_RISE, moved) *
		(1.f - smooth_step(VEHICLE_CAMERA_LIFT_SETTLE, 1.f, moved)) *
		(1.f - smooth_step(VEHICLE_CAMERA_LIFT_WHOLE * radius, VEHICLE_CAMERA_LIFT_BEGIN * radius, across));

	if (lift > 0.f)
	{
		real_vector3d up = { 0.f, 0.f, lift + VEHICLE_CAMERA_CEILING_GAP };
		struct collision_result collision;

		if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
			position, &up, NONE, &collision))
		{
			lift = MAX(0.f, collision.t * up.k - VEHICLE_CAMERA_CEILING_GAP);
		}
		position->z += lift;
	}

	return;
}

/* ---------- public code */

struct observer_result const *vehicle_camera_view(
	short local_player_index,
	struct observer_result const *camera)
{
	struct vehicle_camera *state;
	long unit_index;
	real remaining = 0.f;
	boolean to_first_person = FALSE;
	boolean moving;
	real now;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_NUMBER_OF_LOCAL_PLAYERS || !camera)
	{
		return camera;
	}
	state = &vehicle_cameras[local_player_index];
	state->hidden_unit_index = NONE;
	unit_index = player_control_get_unit_index(local_player_index);
	if (!vehicle_camera_enabled() || unit_index == NONE ||
		!object_try_and_get_and_verify_type(unit_index, _object_mask_unit))
	{
		state->drawn_valid = FALSE;
		state->returning = FALSE;
		state->gliding = FALSE;
		return camera;
	}
	moving = director_seat_camera_moving(local_player_index, &remaining, &to_first_person);
	now = (real)game_time_get() + render_interpolation_fraction();
	state->view = *camera;
	if (moving)
	{
		real_point3d center;
		real radius;

		state->gliding = FALSE;
		/* (the body as the frame draws it) */
		object_get_bounding_sphere(unit_index, &center, &radius);
		render_interpolation_object_point(unit_index, &center);
		vehicle_camera_lift(&state->view.position, &center, radius, to_first_person ? remaining : 1.f - remaining);
		if (distance_squared3d(&state->view.position, &center) < radius * radius)
		{
			state->hidden_unit_index = unit_index;
		}
	}
	else
	{
		/* the director put the view in the eyes with part of the move back
		left (director_update): from where it was drawn, glided */
		if (state->returning && state->drawn_valid &&
			director_get_perspective(local_player_index) == _director_perspective_first_person)
		{
			real_vector3d offset;
			real length_squared;

			vector_from_points3d(&camera->position, &state->drawn_position, &offset);
			length_squared = magnitude_squared3d(&offset);
			if (length_squared > 0.f &&
				length_squared < VEHICLE_CAMERA_GLIDE_LONGEST * VEHICLE_CAMERA_GLIDE_LONGEST)
			{
				/* (from the frame before: moving on this frame, at the speed
				the view came) */
				state->gliding = TRUE;
				state->glide_offset = offset;
				state->glide_start = state->drawn_time;
				state->glide_ticks = MAX(state->returning_remaining, VEHICLE_CAMERA_GLIDE_SHORTEST) *
					(real)TICKS_PER_SECOND;
			}
		}
		if (state->gliding)
		{
			real part = (now - state->glide_start) / state->glide_ticks;

			/* (and none after a load or a new map: an earlier time) */
			if (!(part >= 0.f && part < 1.f))
			{
				state->gliding = FALSE;
			}
			else
			{
				real left = (1.f - part) * (1.f - part);

				state->view.position.x += state->glide_offset.i * left;
				state->view.position.y += state->glide_offset.j * left;
				state->view.position.z += state->glide_offset.k * left;
			}
		}
	}
	state->returning = moving && to_first_person;
	state->returning_remaining = remaining;
	state->drawn_position = state->view.position;
	state->drawn_time = now;
	state->drawn_valid = TRUE;

	return &state->view;
}

boolean vehicle_camera_hides(
	short local_player_index,
	long object_index)
{
	return local_player_index >= 0 && local_player_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS &&
		object_index != NONE && vehicle_cameras[local_player_index].hidden_unit_index == object_index;
}
