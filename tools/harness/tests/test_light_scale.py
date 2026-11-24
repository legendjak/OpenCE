"""The real light_scale_radius (light_scale.c): with per-pixel lighting, a light on an object drawn smaller reaches as
far as the object is scaled; drawn larger, as far as its definition says, as on the Xbox. The light's own radius,
which the units' illumination reads, is left as it was: only the radius it is drawn with is scaled. a10's escape (x30) shrinks the lifepod to a fiftieth beside a miniature of the ring, and its red
cabin light, still a world unit across, painted the ring's hull red when lit for each pixel. Lit for each vertex, as
on the Xbox, it is as it was; lights_preprocess_scene asks it for every dynamic light but a glow."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["scaled", "larger", "per vertex", "as it was"]

SOURCE = "port/linux/game/light_scale.c"

# a fault in the code and the case that must catch it
NEGATIVE_CONTROLS = {
    # not scaled
    "unscaled": (("\treturn radius * object->object.scale;", "\treturn radius;"), "scaled"),
    # scaled lit for each vertex too
    "always": (("if (object_index == NONE || !light_scale_per_pixel())", "if (object_index == NONE)"), "per vertex"),
    # an object of no scale makes the light nothing
    "zero-scale": (("if (!object || !(object->object.scale > 0.0f) ||", "if (!object ||"), "as it was"),
    # grown with an object drawn larger, past the areas that consider it
    "grown": ((" || !(object->object.scale < 1.0f))", ")"), "larger"),
    # the setting read once
    "read-once": (("\tif (changes != config_changes())", "\tif (changes == (unsigned long)-1)"), "per vertex"),
}


def generated(fault=None):
    source = read(SOURCE).replace("\r\n", "\n")
    code = "\n\n".join(function(source, name) for name in ("light_scale_per_pixel", "light_scale_radius")) + "\n"
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


def test_wired():
    """Every dynamic light's radius but a glow's is scaled as it is drawn, not in the light's own radius."""
    lights = function(read("source/objects/object_lights.c").replace("\r\n", "\n"), "lights_preprocess_scene")
    assert "light->radius = light_scale_radius(" not in lights
    submitted = lights.index("light_parameters.radius = light->radius;")
    scaled = lights.index("light_parameters.radius = light_scale_radius(light->object_index, light->radius);", submitted)
    assert "_point_light_port_glow_bit" in lights[submitted:scaled]
    assert scaled < lights.index("rasterizer_light_submit(", scaled)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("light_scale", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("light_scale", generated(fault)), case)
    assert status == CHECK_FAILED, f"light_scale with a fault ({control}) passed '{case}'"
