/*
LENS_FLARE_COVERAGE.C (test)

The real lens_flare_reflection_coverage (rasterizer_lights.c): a lens
flare's reflection smaller than a pixel of the game's 480 lines keeps as
much of its brightness as it covers of one (its width by its height, in
those pixels) is less than what it covers of one of the display's; one of a
pixel or more keeps all of it, and at 480 lines all are kept. The size is
the radius's at the depth, through the window's vertical field of view and
its height in those lines (a split screen's half is 240), by the scale the
sprite is drawn with (a corona's corona_radius_scale).
test_lens_flare_coverage.py takes the code from the sources
(under_test.inc).
*/

#include <math.h>
#include "harness.h"

typedef struct { real i, j; } real_vector2d;
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static struct
{
	struct
	{
		struct { short x0, y0, x1, y1; } viewport_bounds;
		real vertical_field_of_view;
	} camera;
} global_window_parameters;

/* the display's pixels to one of the game's 480 lines */
static float screen_scale = 5.0f;
static float halo_screen_scale(void) { return screen_scale; }
static real_vector2d const unscaled = { 1.0f, 1.0f };

#include "under_test.inc"

#define NEAR(a, b) (fabs((a) - (b)) < 1e-4)

/* the window: 480 lines (or 240), a vertical field of view whose half's
tangent is 1, so that a radius r at depth d is r/d*240 pixels */
static void window(short lines)
{
	global_window_parameters.camera.viewport_bounds.x0 = 0;
	global_window_parameters.camera.viewport_bounds.y0 = 0;
	global_window_parameters.camera.viewport_bounds.x1 = 640;
	global_window_parameters.camera.viewport_bounds.y1 = lines;
	global_window_parameters.camera.vertical_field_of_view = 1.5707963f; /* (a right angle: tan of half is 1) */
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	window(480);
	/* d40's flames: a fifth of a pixel across keeps 4%, on a display of a
	pixel or more for it */
	CASE("small")
	{
		real coverage = lens_flare_reflection_coverage(0.2f, &unscaled, 480.0f);

		CHECK(NEAR(coverage, 0.04), "a fifth of a pixel keeps %f", coverage);
		coverage = lens_flare_reflection_coverage(0.25f, &unscaled, 240.0f);
		CHECK(NEAR(coverage, 0.25), "half a pixel keeps %f", coverage);
		return 0;
	}
	/* at 480 lines the display shows it as the Xbox did: all of it kept */
	CASE("native")
	{
		screen_scale = 1.0f;
		CHECK(lens_flare_reflection_coverage(0.2f, &unscaled, 480.0f) == 1.0f, "dimmed at 480 lines");
		screen_scale = 0.75f;
		CHECK(lens_flare_reflection_coverage(0.2f, &unscaled, 480.0f) == 1.0f, "dimmed below 480 lines");
		return 0;
	}
	/* a display of 960 lines shows a fifth of a pixel as 0.4 of one of its
	own, now and then: dimmed by 0.04 of 0.16 */
	CASE("between")
	{
		real coverage;

		screen_scale = 2.0f;
		coverage = lens_flare_reflection_coverage(0.2f, &unscaled, 480.0f);
		CHECK(NEAR(coverage, 0.25), "a fifth of a pixel at 960 lines keeps %f", coverage);
		return 0;
	}
	/* a corona stretched across pixels one way: dimmed by its other way only,
	and not at all stretched past a pixel both ways */
	CASE("stretched")
	{
		real_vector2d stretched = { 5.0f, 1.0f };
		real coverage = lens_flare_reflection_coverage(0.2f, &stretched, 480.0f);

		CHECK(NEAR(coverage, 0.2), "a corona a pixel wide and a fifth tall keeps %f", coverage);
		stretched.i = stretched.j = -6.0f;
		CHECK(lens_flare_reflection_coverage(0.2f, &stretched, 480.0f) == 1.0f, "stretched past a pixel dimmed");
		return 0;
	}
	/* a pixel or more keeps all of it */
	CASE("large")
	{
		CHECK(lens_flare_reflection_coverage(1.0f, &unscaled, 480.0f) == 1.0f, "a pixel dimmed");
		CHECK(lens_flare_reflection_coverage(10.0f, &unscaled, 100.0f) == 1.0f, "a large one dimmed");
		return 0;
	}
	/* a split screen's half: half the lines, half the diameter */
	CASE("split")
	{
		real coverage;

		window(240);
		coverage = lens_flare_reflection_coverage(0.5f, &unscaled, 240.0f);
		CHECK(NEAR(coverage, 0.25), "half a pixel of a half screen keeps %f", coverage);
		return 0;
	}
	/* no depth, no window, a radius not a number: left as it is */
	CASE("degenerate")
	{
		CHECK(lens_flare_reflection_coverage(1.0f, &unscaled, 0.0f) == 1.0f, "no depth dimmed");
		CHECK(lens_flare_reflection_coverage(1.0f, &unscaled, -5.0f) == 1.0f, "behind dimmed");
		CHECK(lens_flare_reflection_coverage((real)NAN, &unscaled, 10.0f) == 1.0f, "not a number dimmed");
		window(0);
		CHECK(lens_flare_reflection_coverage(0.1f, &unscaled, 10.0f) == 1.0f, "no window dimmed");
		return 0;
	}

	return 2;
}
