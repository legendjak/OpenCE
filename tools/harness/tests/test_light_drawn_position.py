"""The objects a light is chosen for as they are drawn are those it reaches from where the frame draws it (a marine's
flashlight, on the marker of the rifle he holds, from the pose drawn between the ticks), as it lights them; the
units' illumination, found as the game is simulated, is from the light's own place, the tick's
(object_lights.c's lights_port_drawn_position)."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import function, read  # noqa: E402

SOURCE = "source/objects/object_lights.c"


def lights():
    return read(SOURCE).replace("\r\n", "\n")


def test_submitted_place_kept():
    """Each light submitted for the window keeps where it is drawn, by its rasterizer light's index."""
    prepare = function(lights(), "lights_preprocess_scene")
    submitted = prepare.index("light->rasterizer_light_index = rasterizer_light_submit(")
    kept = prepare.index("lights_port.drawn_positions[light->rasterizer_light_index] = light_parameters.position;",
                         submitted)
    # (after the marker's drawn pose is taken, and only for an index the window has room for)
    assert prepare.index("light_parameters.position = markers[0].matrix.position;") < submitted < kept
    assert "light->rasterizer_light_index < MAXIMUM_LIGHTS_PER_WINDOW" in prepare[submitted:kept]


def test_drawn_place_or_own():
    """A light submitted is drawn from its rasterizer light's place; one not submitted is its own, the tick's."""
    drawn = function(lights(), "lights_port_drawn_position")
    assert "light->rasterizer_light_index >= 0 && light->rasterizer_light_index < MAXIMUM_LIGHTS_PER_WINDOW" in drawn
    assert "return &lights_port.drawn_positions[light->rasterizer_light_index];" in drawn
    assert drawn.rstrip().endswith("return &light->position;\n}")


def test_chosen_as_drawn_and_simulated():
    """Objects drawn choose their lights from where they are drawn; the illumination at a point, from the lights'
    own places; the clusters' kept lights hold both, and each search uses the one it is asked for."""
    source = lights()
    dynamic = function(source, "lights_prepare_for_object_dynamic")
    assert "MAXIMUM_RENDERED_POINT_LIGHTS,\n\t\t\tTRUE);" in dynamic
    illumination = function(source, "lights_illumination_at_point")
    assert "MAXIMUM_RENDERED_POINT_LIGHTS,\n\t\t\tFALSE);" in illumination
    cluster = function(source, "lights_port_cluster")
    assert "entry->position = light->position;" in cluster
    assert "entry->drawn_position = *lights_port_drawn_position(light);" in cluster
    search = function(source, "find_point_lights_for_object_in_cluster")
    assert "real_point3d const *position = drawn ? &entry->drawn_position : &entry->position;" in search
    assert "entry->position." not in search
    assert search.count(", drawn);") == 2
    light = function(source, "find_point_light_for_object")
    assert "distance3d(drawn ? lights_port_drawn_position(light) : &light->position, center)" in light
