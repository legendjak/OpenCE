/*
CAMPAIGN_SHADE.C

The campaign Shade's look on Halo PC's multiplayer Shade (custom-387).

Halo PC's multiplayer Shade (vehicles\c gun turret\c gun turret_mp, on
Halo PC's maps, and on the Xbox's Sidewinder with Halo PC's items:
gearbox_items.c) fires a projectile of its own, which Gearbox drew apart
from the campaign's. Its contrail (SHADE_CONTRAIL_NAME) is wider and tinted,
of four states, and made of a third as many points (10 a second, where the
Xbox's campaign contrail makes 30: Halo PC's campaign one has 10 too); its
firing effect (SHADE_FIRE_NAME) is dim (its flashes at three tenths' alpha,
its glow a faint violet) and has fewer particles, and not the campaign's last
flash; the smoke the projectile trails (SHADE_BEAM_NAME) is larger, and its
light (SHADE_LIGHT_NAME) reaches farther. The particles they draw are the
same tags as the campaign's, and alike on Halo PC and the Xbox.

As a map's tags load, each of those four that is as Gearbox made it (the
values the Xbox's campaign Shade's differ in, checked) is given the
campaign's (vehicles\c gun turret\c gun turret, its effects "c gun turret
fire" and "c gun turret beam" and its light "plasma", as the Xbox's b30 has
them): the contrail its point rate and states, the firing effect its counts,
colors and last flash, the smoke its size and the light its radius. A tag
otherwise (a mod's) is left as it is. The projectile's flight and damage stay
Halo PC's: its bolts fly at 60 world units a second (the campaign's at 25),
so the trail is drawn longer. Each value changed is kept, and given back as
the tags unload (campaign_shade_tags_unloaded), with the block that holds the
last flash. Only the drawing changes, nothing played or sent between
machines. display.campaign_shade turns it off.
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "math/real_math.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "effects/contrail_definitions.h"
#include "effects/effect_definitions.h"

#include "campaign_shade.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

#define SHADE_CONTRAIL_NAME "vehicles\\c gun turret\\mp gun turret"
#define SHADE_FIRE_NAME "vehicles\\c gun turret\\effects\\mp gun turret fire"
#define SHADE_BEAM_NAME "vehicles\\c gun turret\\effects\\mp gun turret beam"
#define SHADE_LIGHT_NAME "vehicles\\c gun turret\\mp plasma"
/* the particles the firing effect and the smoke draw */
#define SHADE_CHARGE_NAME "weapons\\plasma rifle\\plasma rifle charge"
#define SHADE_SMOKE_NAME "effects\\particles\\energy\\smoke white c generic"
#define SHADE_FLASH_MEDIUM_NAME "effects\\particles\\energy\\flash c generic med"
#define SHADE_FLASH_NAME "effects\\particles\\energy\\flash c generic"
#define SHADE_BEAM_SMOKE_NAME "effects\\particles\\energy\\smoke c generic"

enum
{
	SHADE_EFFECT_TAG = 'effe',
	SHADE_LIGHT_TAG = 'ligh',
	SHADE_PARTICLE_TAG = 'part',

	/* the contrail's states: Gearbox's, the campaign's */
	SHADE_CONTRAIL_STATES = 4,
	CAMPAIGN_CONTRAIL_STATES = 3,
	/* the firing effect's events, and the first's particles: Gearbox's, the
	campaign's (its last flash added) */
	SHADE_FIRE_EVENTS = 2,
	SHADE_FIRE_PARTICLES = 5,
	CAMPAIGN_FIRE_PARTICLES = 6,

	/* the values changed, kept to be given back */
	MAXIMUM_SHADE_CHANGES = 16
};

/* how near a value of the tags must be to Gearbox's to be its */
#define SHADE_VALUE_TOLERANCE 0.00001f

/* ---------- structures */

/* an effect event's particle (effects.c's effect_particles_definition) */
struct shade_effect_particle
{
	short environment;
	short disposition;
	short camera_mode;
	short unused006;
	short location_index;
	short unused00a;
	real_euler_angles2d direction;
	real_vector3d offset;
	real_vector3d runtime_direction;
	long unused02c[10];
	struct tag_reference particle;
	unsigned long flags;
	short distribution_function;
	short unused06a;
	short count_lower_bound;
	short count_upper_bound;
	real distribution_radius_lower_bound;
	real distribution_radius_upper_bound;
	long unused078[3];
	real velocity_lower_bound;
	real velocity_upper_bound;
	real velocity_cone_angle;
	real angular_velocity_lower_bound;
	real angular_velocity_upper_bound;
	long unused098[2];
	real radius_lower_bound;
	real radius_upper_bound;
	long unused0a8[2];
	real_argb_color tint_lower_bound;
	real_argb_color tint_upper_bound;
	long unused0d0[4];
	unsigned long scale_a_flags;
	unsigned long scale_b_flags;
};

typedef char verify_shade_effect_particle_size[sizeof(struct shade_effect_particle) == 0xE8 ? 1 : -1];
typedef char verify_shade_effect_particle_reference_offset[
	offsetof(struct shade_effect_particle, particle) == 0x54 ? 1 : -1];
typedef char verify_shade_effect_particle_count_offset[
	offsetof(struct shade_effect_particle, count_lower_bound) == 0x6C ? 1 : -1];
typedef char verify_shade_effect_particle_tint_offset[
	offsetof(struct shade_effect_particle, tint_lower_bound) == 0xB0 ? 1 : -1];

/* a light definition's first fields (object_lights.c's point_light_definition) */
struct shade_light
{
	long flags;
	real radius;
	real radius_modifier_lower_bound;
	real radius_modifier_upper_bound;
	real falloff_angle;
	real cutoff_angle;
	real lens_flare_radius;
};

typedef char verify_shade_light_lens_flare_radius_offset[
	offsetof(struct shade_light, lens_flare_radius) == 0x18 ? 1 : -1];

/* bytes of the tags changed, and what they held */
struct shade_change
{
	void *address;
	unsigned long size;
	void *original;
};

/* ---------- globals */

static struct
{
	struct shade_change changes[MAXIMUM_SHADE_CHANGES];
	short change_count;
	/* the firing effect's first event's particles, the campaign's (NULL: none
	made) */
	struct shade_effect_particle *fire_particles;
} campaign_shade;

/* ---------- prototypes */

int config_boolean(char const *name);

/* ---------- private code */

static boolean shade_value(
	real value,
	real gearbox_value)
{
	return (real)fabs(value - gearbox_value) <= SHADE_VALUE_TOLERANCE;
}

/* whether the reference names the loaded tag of `group_tag` named `name` */
static boolean shade_reference_is(
	struct tag_reference const *reference,
	unsigned long group_tag,
	char const *name)
{
	char const *tag_name;

	if (reference->group_tag != group_tag || reference->index == NONE)
		return FALSE;
	tag_name = tag_get_name(reference->index);

	return tag_name && !strcmp(tag_name, name);
}

/* `size` bytes of the tags at `address` kept, to be given back as they
unload: FALSE (to change nothing) if they cannot be */
static boolean shade_change(
	void *address,
	unsigned long size)
{
	struct shade_change *change;

	if (campaign_shade.change_count >= MAXIMUM_SHADE_CHANGES)
		return FALSE;
	change = &campaign_shade.changes[campaign_shade.change_count];
	change->original = malloc(size);
	if (!change->original)
		return FALSE;
	memcpy(change->original, address, size);
	change->address = address;
	change->size = size;
	campaign_shade.change_count++;

	return TRUE;
}

/* the contrail: the campaign's 30 points a second, and its three states
(Gearbox's first, narrower, white and held a moment; its third, fading
sooner; its last) */
static boolean campaign_shade_contrail(
	void)
{
	long tag_index = tag_loaded(CONTRAIL_DEFINITION_TAG, SHADE_CONTRAIL_NAME);
	struct contrail_definition *contrail;
	struct contrail_point_state *states;
	struct contrail_point_state campaign[CAMPAIGN_CONTRAIL_STATES];

	if (tag_index == NONE)
		return FALSE;
	contrail = tag_get(CONTRAIL_DEFINITION_TAG, tag_index);
	states = contrail && contrail->states.count == SHADE_CONTRAIL_STATES ?
		TAG_BLOCK_GET_ELEMENT(&contrail->states, 0, struct contrail_point_state) :
		NULL;
	if (!states || !shade_value(contrail->point_generation_rate, 10.f) ||
		!shade_value(states[0].width, 0.25f) || !shade_value(states[1].width, 0.2f) ||
		!shade_value(states[2].transition_duration_lower_bound, 0.0399999991f) ||
		!shade_value(states[2].color_lower_bound.red, 0.976471007f) ||
		states[0].physics.index == NONE || states[2].physics.index != states[0].physics.index ||
		states[3].physics.index != NONE)
	{
		return FALSE;
	}

	campaign[0] = states[0];
	campaign[0].duration_lower_bound = campaign[0].duration_upper_bound = 0.00999999978f;
	campaign[0].transition_duration_lower_bound = campaign[0].transition_duration_upper_bound = 0.0500000007f;
	campaign[0].width = 0.150000006f;
	campaign[0].color_lower_bound.alpha = campaign[0].color_upper_bound.alpha = 1.f;
	campaign[0].color_lower_bound.red = campaign[0].color_upper_bound.red = 1.f;
	campaign[0].color_lower_bound.green = campaign[0].color_upper_bound.green = 1.f;
	campaign[0].color_lower_bound.blue = campaign[0].color_upper_bound.blue = 1.f;
	campaign[1] = states[2];
	campaign[1].transition_duration_lower_bound = campaign[1].transition_duration_upper_bound = 0.00499999989f;
	campaign[2] = states[3];

	if (!shade_change(&contrail->point_generation_rate, sizeof(contrail->point_generation_rate)) ||
		!shade_change(&contrail->states.count, sizeof(contrail->states.count)) ||
		!shade_change(states, CAMPAIGN_CONTRAIL_STATES * sizeof(*states)))
	{
		return FALSE;
	}
	contrail->point_generation_rate = 30.f;
	memcpy(states, campaign, sizeof(campaign));
	contrail->states.count = CAMPAIGN_CONTRAIL_STATES;

	return TRUE;
}

/* the particle's count, lower and upper bounds */
static void shade_particle_count(
	struct shade_effect_particle *particle,
	short lower,
	short upper)
{
	particle->count_lower_bound = lower;
	particle->count_upper_bound = upper;

	return;
}

/* the firing effect: the campaign's counts and colors, and its last flash
(of the map's flash particle, when it has it) */
static boolean campaign_shade_fire(
	void)
{
	long tag_index = tag_loaded(SHADE_EFFECT_TAG, SHADE_FIRE_NAME);
	long flash_index = tag_loaded(SHADE_PARTICLE_TAG, SHADE_FLASH_NAME);
	struct effect_definition *effect;
	struct effect_event_definition *events;
	struct shade_effect_particle *particles;
	struct shade_effect_particle *smoke;
	struct shade_effect_particle *campaign;
	long count;
	short index;

	if (tag_index == NONE)
		return FALSE;
	effect = tag_get(SHADE_EFFECT_TAG, tag_index);
	events = effect && effect->events.count == SHADE_FIRE_EVENTS ?
		TAG_BLOCK_GET_ELEMENT(&effect->events, 0, struct effect_event_definition) :
		NULL;
	particles = events && events[0].particles.count == SHADE_FIRE_PARTICLES ?
		TAG_BLOCK_GET_ELEMENT(&events[0].particles, 0, struct shade_effect_particle) :
		NULL;
	smoke = events && events[1].particles.count == 1 ?
		TAG_BLOCK_GET_ELEMENT(&events[1].particles, 0, struct shade_effect_particle) :
		NULL;
	if (!particles || !smoke ||
		!shade_reference_is(&particles[0].particle, SHADE_PARTICLE_TAG, SHADE_CHARGE_NAME) ||
		!shade_reference_is(&particles[1].particle, SHADE_PARTICLE_TAG, SHADE_SMOKE_NAME) ||
		!shade_reference_is(&smoke->particle, SHADE_PARTICLE_TAG, SHADE_SMOKE_NAME) ||
		particles[0].count_lower_bound != 1 || particles[0].count_upper_bound != 1 ||
		particles[1].count_lower_bound != 1 || particles[1].count_upper_bound != 3 ||
		smoke->count_lower_bound != 1 || smoke->count_upper_bound != 3 ||
		!shade_value(particles[0].tint_lower_bound.alpha, 0.300000012f) ||
		!shade_value(particles[0].tint_lower_bound.blue, 0.5f))
	{
		return FALSE;
	}
	for (index = 2; index < SHADE_FIRE_PARTICLES; index++)
	{
		if (!shade_reference_is(&particles[index].particle, SHADE_PARTICLE_TAG, SHADE_FLASH_MEDIUM_NAME) ||
			particles[index].count_lower_bound != 2 || particles[index].count_upper_bound != 2 ||
			!shade_value(particles[index].tint_lower_bound.alpha, 0.300000012f) ||
			!shade_value(particles[index].tint_upper_bound.alpha, 0.300000012f))
		{
			return FALSE;
		}
	}

	count = flash_index != NONE ? CAMPAIGN_FIRE_PARTICLES : SHADE_FIRE_PARTICLES;
	campaign = malloc(CAMPAIGN_FIRE_PARTICLES * sizeof(*campaign));
	if (!campaign)
		return FALSE;
	memcpy(campaign, particles, SHADE_FIRE_PARTICLES * sizeof(*campaign));

	/* (the charge's glow: none, and pale violet at full alpha) */
	shade_particle_count(&campaign[0], 0, 0);
	campaign[0].radius_upper_bound = 0.300000012f;
	campaign[0].tint_lower_bound.alpha = campaign[0].tint_upper_bound.alpha = 1.f;
	campaign[0].tint_lower_bound.red = 0.458824009f;
	campaign[0].tint_upper_bound.red = 0.458799988f;
	campaign[0].tint_lower_bound.green = campaign[0].tint_upper_bound.green = 0.400000006f;
	campaign[0].tint_lower_bound.blue = campaign[0].tint_upper_bound.blue = 1.f;
	/* (the smoke, and the three flashes at full alpha) */
	shade_particle_count(&campaign[1], 3, 5);
	shade_particle_count(&campaign[2], 2, 3);
	shade_particle_count(&campaign[3], 3, 4);
	shade_particle_count(&campaign[4], 2, 3);
	for (index = 2; index < SHADE_FIRE_PARTICLES; index++)
		campaign[index].tint_lower_bound.alpha = campaign[index].tint_upper_bound.alpha = 1.f;
	/* (the last flash: the third's, of the larger flash, straight ahead,
	faster, smaller and paler) */
	if (count == CAMPAIGN_FIRE_PARTICLES)
	{
		struct shade_effect_particle *flash = &campaign[CAMPAIGN_FIRE_PARTICLES - 1];
		char const *flash_name = tag_get_name(flash_index);

		*flash = campaign[4];
		flash->direction.yaw = flash->direction.pitch = 0.f;
		flash->runtime_direction.i = 1.f;
		flash->runtime_direction.j = flash->runtime_direction.k = 0.f;
		flash->particle.group_tag = SHADE_PARTICLE_TAG;
		flash->particle.name = (char *)flash_name;
		flash->particle.name_length = flash_name ? (long)strlen(flash_name) : 0;
		flash->particle.index = flash_index;
		flash->velocity_lower_bound = 1.5f;
		flash->velocity_upper_bound = 2.5f;
		flash->radius_lower_bound = flash->radius_upper_bound = 0.25f;
		flash->tint_lower_bound.red = 0.427451015f;
		flash->tint_upper_bound.red = 0.474510014f;
		flash->tint_lower_bound.green = flash->tint_upper_bound.green = 0.384314001f;
	}

	if (!shade_change(&events[0].particles, sizeof(events[0].particles)) ||
		!shade_change(&smoke->count_lower_bound, 2 * sizeof(smoke->count_lower_bound)))
	{
		free(campaign);
		return FALSE;
	}
	campaign_shade.fire_particles = campaign;
	events[0].particles.count = count;
	events[0].particles.address = campaign;
	shade_particle_count(smoke, 5, 8);

	return TRUE;
}

/* the smoke the projectile trails: the campaign's, smaller */
static boolean campaign_shade_beam(
	void)
{
	long tag_index = tag_loaded(SHADE_EFFECT_TAG, SHADE_BEAM_NAME);
	struct effect_definition *effect;
	struct effect_event_definition *event;
	struct shade_effect_particle *smoke;

	if (tag_index == NONE)
		return FALSE;
	effect = tag_get(SHADE_EFFECT_TAG, tag_index);
	event = effect && effect->events.count == 1 ?
		TAG_BLOCK_GET_ELEMENT(&effect->events, 0, struct effect_event_definition) :
		NULL;
	smoke = event && event->particles.count == 1 ?
		TAG_BLOCK_GET_ELEMENT(&event->particles, 0, struct shade_effect_particle) :
		NULL;
	if (!smoke || !shade_reference_is(&smoke->particle, SHADE_PARTICLE_TAG, SHADE_BEAM_SMOKE_NAME) ||
		!shade_value(smoke->radius_lower_bound, 0.349999994f) || !shade_value(smoke->radius_upper_bound, 0.5f) ||
		!shade_change(&smoke->radius_lower_bound, 2 * sizeof(smoke->radius_lower_bound)))
	{
		return FALSE;
	}
	smoke->radius_lower_bound = 0.200000003f;
	smoke->radius_upper_bound = 0.349999994f;

	return TRUE;
}

/* the projectile's light: the campaign's radius, and its lens flare's */
static boolean campaign_shade_light(
	void)
{
	long tag_index = tag_loaded(SHADE_LIGHT_TAG, SHADE_LIGHT_NAME);
	struct shade_light *light;

	if (tag_index == NONE)
		return FALSE;
	light = tag_get(SHADE_LIGHT_TAG, tag_index);
	if (!light || !shade_value(light->radius, 1.f) || !shade_value(light->lens_flare_radius, 1.f) ||
		!shade_change(&light->radius, sizeof(light->radius)) ||
		!shade_change(&light->lens_flare_radius, sizeof(light->lens_flare_radius)))
	{
		return FALSE;
	}
	light->radius = light->lens_flare_radius = 0.699999988f;

	return TRUE;
}

/* what was changed let go, given back to the tags first when `give_back`
(they are still loaded) */
static void campaign_shade_forget(
	boolean give_back)
{
	/* (newest first: a block's count and address before what was in it) */
	while (campaign_shade.change_count > 0)
	{
		struct shade_change *change = &campaign_shade.changes[--campaign_shade.change_count];

		if (give_back)
			memcpy(change->address, change->original, change->size);
		free(change->original);
		change->original = NULL;
	}
	if (campaign_shade.fire_particles)
	{
		free(campaign_shade.fire_particles);
		campaign_shade.fire_particles = NULL;
	}

	return;
}

/* ---------- public code */

void campaign_shade_tags_loaded(
	void)
{
	short changed;

	/* (anything still kept is of tags gone: let go, not given back) */
	campaign_shade_forget(FALSE);
	if (!config_boolean("display.campaign_shade") || tag_loaded(CONTRAIL_DEFINITION_TAG, SHADE_CONTRAIL_NAME) == NONE)
		return;
	changed = (short)campaign_shade_contrail() + (short)campaign_shade_fire() + (short)campaign_shade_beam() +
		(short)campaign_shade_light();
	error(_error_silent, "campaign shade: %d of the multiplayer Shade's 4 tags drawn as the campaign's", (int)changed);

	return;
}

void campaign_shade_tags_unloaded(
	void)
{
	campaign_shade_forget(TRUE);

	return;
}
