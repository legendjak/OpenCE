"""The real lens_flare_reflection_coverage (rasterizer_lights.c): a lens flare's reflection smaller than a pixel of the
game's 480 lines is dimmed by as much of one as it covers, as the Xbox drew it now and then on the pixels whose centres
it fell on, not drawn as a steady dot at the display's resolution; one of a pixel or more is drawn as it was."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["small", "native", "between", "stretched", "large", "split", "degenerate"]

# a fault in the code and the case that must catch it
NEGATIVE_CONTROLS = {
    # never dimmed
    "never": (("? MIN(game_coverage / display_coverage, 1.0f) : 1.0f;",
               "? MIN(game_coverage / display_coverage, 1.0f) * 0.0f + 1.0f : 1.0f;"), "small"),
    # dimmed at 480 lines too, where the display already shows it as the Xbox did
    "native": (("MIN(game_coverage / display_coverage, 1.0f)", "game_coverage"), "native"),
    # the corona's scale not counted (its radius alone)
    "unscaled": (("	width = diameter * (scale->i < 0.0f ? -scale->i : scale->i);", "	width = diameter;"),
                 "stretched"),
    # the window's lines not counted (as though always 480)
    "lines": (("	diameter = 2.0f * radius / depth * half_height / tangent;",
               "	diameter = 2.0f * radius / depth * 240.0f / tangent;"), "split"),
}


def generated(fault=None):
    source = read("source/rasterizer/rasterizer_lights.c").replace("\r\n", "\n")
    code = function(source, "lens_flare_reflection_coverage") + "\n"
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


def test_drawn_dimmed():
    """each reflection's brightness is dimmed by its coverage, with the radius and scale as drawn, before its color."""
    source = read("source/rasterizer/rasterizer_lights.c").replace("\r\n", "\n")
    draw = function(source, "rasterizer_lens_flares_draw")
    dimmed = draw.index("brightness *= lens_flare_reflection_coverage(")
    assert draw.index("real radius = radius_lower_bound+") < dimmed < draw.index("if (reflection->tint_color.alpha == 0.0f &&")
    # with the scale the sprite is drawn with: the corona's for the first reflection
    assert "drawn_scale = definition->corona_radius_scale;" in draw[:dimmed]
    for flag in ("_lens_flare_reflection_radius_scaled_by_occlusion_bit",
                 "_lens_flare_reflection_radius_not_scaled_by_distance_bit"):
        assert flag in draw[dimmed:draw.index("if (reflection->tint_color.alpha == 0.0f &&")]


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("lens_flare_coverage", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("lens_flare_coverage", generated(fault)), case)
    assert status == CHECK_FAILED, f"lens_flare_reflection_coverage with a fault ({control}) passed '{case}'"
