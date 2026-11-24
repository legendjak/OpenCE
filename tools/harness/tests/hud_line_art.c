/*
HUD_LINE_ART.C (test)

The real upscale_pixels and upscale_sharpen_alpha
(hud_hires.c): the message icons' sheets (the weapons the pickup prompt
shows, the buttons and sticks) have no redraw, and were drawn from the map's
texels by interpolation alone, 4 times their size: their outlines a texel
or two wide were soft at a screen of 1080 lines and more. They are drawn 8
times their size, their coverage sharpened by an unsharp mask: an edge
steeper, a flat part as it was.
test_hud_line_art.py takes the code from the sources (under_test.inc).
*/

#include "harness.h"

#include "config.inc"

#include "under_test.inc"

#define SIDE 16

/* a BGRA sheet: white, its alpha an outline a texel wide (column 5), a
flat tinted fill (columns 9 to 14, alpha 90) and nothing else */
static unsigned char sheet[SIDE * SIDE * 4];

static void sheet_reset(void)
{
	long x, y;

	for (y = 0; y < SIDE; y++)
	{
		for (x = 0; x < SIDE; x++)
		{
			unsigned char *texel = sheet + (y * SIDE + x) * 4;

			texel[0] = texel[1] = texel[2] = 255;
			texel[3] = x == 5 ? 255 : x >= 9 && x <= 14 ? 90 : 0;
		}
	}
}

/* across the outline, the upscaled texels' width where its alpha is over
a quarter and under three quarters: its soft edge, in texels */
static long soft_width(unsigned char const *pixels, long factor)
{
	long width = SIDE * factor, y = SIDE * factor / 2, x, soft = 0;

	for (x = 2 * factor; x < 8 * factor; x++)
	{
		unsigned char alpha = pixels[(y * width + x) * 4 + 3];

		soft += alpha > 64 && alpha < 192;
	}
	return soft;
}

/* the unsharp mask as it was first written, over whole planes of floats: the
banded upscale_sharpen_alpha must give the same texels */
static void reference_sharpen_alpha(unsigned char *pixels, long width, long height)
{
	float kernel[64];
	long radius = (long)(3.0f * UPSCALE_SHARPEN_SIGMA + 0.5f), tap, x, y, index;
	float total = 0.0f;
	float *alpha = malloc(width * height * sizeof(float));
	float *across = malloc(width * height * sizeof(float));
	float *blurred = malloc(width * height * sizeof(float));

	for (tap = 0; tap <= radius; tap++)
	{
		float weight = 1.0f;
		long power;

		for (power = 0; power < tap * tap; power++)
			weight *= UPSCALE_SHARPEN_STEP;
		kernel[radius + tap] = kernel[radius - tap] = weight;
		total += tap ? 2.0f * weight : weight;
	}
	for (tap = 0; tap <= radius * 2; tap++)
		kernel[tap] /= total;
	for (index = 0; index < width * height; index++)
		alpha[index] = pixels[index * 4 + 3];
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
		{
			float sum = 0.0f;

			for (tap = -radius; tap <= radius; tap++)
			{
				long sx = x + tap < 0 ? 0 : x + tap >= width ? width - 1 : x + tap;

				sum += alpha[y * width + sx] * kernel[tap + radius];
			}
			across[y * width + x] = sum;
		}
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
		{
			float sum = 0.0f;

			for (tap = -radius; tap <= radius; tap++)
			{
				long sy = y + tap < 0 ? 0 : y + tap >= height ? height - 1 : y + tap;

				sum += across[sy * width + x] * kernel[tap + radius];
			}
			blurred[y * width + x] = sum;
		}
	for (index = 0; index < width * height; index++)
	{
		float value = alpha[index] + UPSCALE_SHARPEN_AMOUNT * (alpha[index] - blurred[index]);

		pixels[index * 4 + 3] = (unsigned char)(value < 0.0f ? 0 : value > 255.0f ? 255 : value + 0.5f);
	}
	free(alpha);
	free(across);
	free(blurred);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	/* a sheet upscaled past a band of rows (48x48 to 384x384), its coverage
	made of lines and fills every way: sharpened a band at a time, the same
	texels as over the whole texture at once */
	CASE("banded")
	{
		enum { LARGE = 48, LARGE_WIDTH = LARGE * UPSCALE_LINE_ART_FACTOR };
		static unsigned char large[LARGE * LARGE * 4];
		unsigned char *banded, *whole;
		long x, y, differences = 0;

		CHECK(LARGE_WIDTH > UPSCALE_SHARPEN_BAND, "the sheet within one band");
		for (y = 0; y < LARGE; y++)
			for (x = 0; x < LARGE; x++)
			{
				unsigned char *texel = large + (y * LARGE + x) * 4;

				texel[0] = texel[1] = texel[2] = 255;
				texel[3] = (unsigned char)((x * 7 + y * 13) % 5 == 0 ? 255 : (x + y) % 9 < 3 ? 90 : (x ^ y) & 1 ? 40 : 0);
			}
		banded = upscale_pixels(large, LARGE, LARGE, 256, UPSCALE_LINE_ART_FACTOR);
		whole = upscale_pixels(large, LARGE, LARGE, 256, UPSCALE_LINE_ART_FACTOR);
		CHECK(banded && whole, "not upscaled");
		upscale_sharpen_alpha(banded, LARGE_WIDTH, LARGE_WIDTH);
		reference_sharpen_alpha(whole, LARGE_WIDTH, LARGE_WIDTH);
		for (x = 0; x < LARGE_WIDTH * LARGE_WIDTH * 4; x++)
			differences += banded[x] != whole[x];
		CHECK(!differences, "%ld bytes not as over the whole texture", differences);
		free(banded);
		free(whole);
		return 0;
	}

	sheet_reset();

	/* 8 times the sheet's size, its alignment kept: the outline's texel
	centred where it was */
	CASE("size")
	{
		unsigned char *pixels = upscale_pixels(sheet, SIDE, SIDE, 256, UPSCALE_LINE_ART_FACTOR);
		long width = SIDE * UPSCALE_LINE_ART_FACTOR, y = width / 2, peak = 0, x;

		CHECK(pixels != NULL, "not upscaled");
		for (x = 1; x < width; x++)
			if (pixels[(y * width + x) * 4 + 3] > pixels[(y * width + peak) * 4 + 3])
				peak = x;
		CHECK(peak == 5 * UPSCALE_LINE_ART_FACTOR + UPSCALE_LINE_ART_FACTOR / 2 ||
			peak == 5 * UPSCALE_LINE_ART_FACTOR + UPSCALE_LINE_ART_FACTOR / 2 - 1, "the outline at %ld", peak);
		free(pixels);
		return 0;
	}
	/* sharpened: the outline's edges steeper than interpolation leaves
	them */
	CASE("steeper")
	{
		unsigned char *soft = upscale_pixels(sheet, SIDE, SIDE, 256, UPSCALE_LINE_ART_FACTOR);
		unsigned char *sharp = upscale_pixels(sheet, SIDE, SIDE, 256, UPSCALE_LINE_ART_FACTOR);
		long before, after;

		CHECK(soft && sharp, "not upscaled");
		upscale_sharpen_alpha(sharp, SIDE * UPSCALE_LINE_ART_FACTOR, SIDE * UPSCALE_LINE_ART_FACTOR);
		before = soft_width(soft, UPSCALE_LINE_ART_FACTOR);
		after = soft_width(sharp, UPSCALE_LINE_ART_FACTOR);
		CHECK(after * 3 <= before * 2, "edges %ld texels soft, %ld before", after, before);
		free(soft);
		free(sharp);
		return 0;
	}
	/* the flat fill's alpha as it was, inside it, and nothing where the
	sheet had nothing */
	CASE("flat kept")
	{
		unsigned char *sharp = upscale_pixels(sheet, SIDE, SIDE, 256, UPSCALE_LINE_ART_FACTOR);
		long width = SIDE * UPSCALE_LINE_ART_FACTOR, y = width / 2;
		unsigned char inside, outside;

		CHECK(sharp != NULL, "not upscaled");
		upscale_sharpen_alpha(sharp, width, width);
		inside = sharp[(y * width + 12 * UPSCALE_LINE_ART_FACTOR) * 4 + 3];
		outside = sharp[(y * width + 1 * UPSCALE_LINE_ART_FACTOR) * 4 + 3];
		CHECK(inside >= 88 && inside <= 92, "the fill's alpha %d", inside);
		CHECK(outside == 0, "alpha %d where there was none", outside);
		CHECK(sharp[(y * width + 12 * UPSCALE_LINE_ART_FACTOR) * 4] == 255, "colour changed");
		free(sharp);
		return 0;
	}

	return 2;
}
