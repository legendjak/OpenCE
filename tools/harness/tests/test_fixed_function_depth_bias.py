"""With display.reversed_depth the hardware's constant polygon offset is off for every draw and a z bias is added in
the Xbox's depth units by the vertex program (nv2a_vsh.c, d3d8_gl.c's depth_bias): the fixed-function program (the
immediate-mode draws, the loading screen's) adds it too, after the depth is reversed, as a translated program does."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import function, read  # noqa: E402


def test_fixed_function_biased():
    source = read("port/linux/src/nv2a_vsh.c").replace("\r\n", "\n")
    fixed = function(source, "nv2a_fixed_function_glsl")
    reversed_ = fixed.index('"\\tgl_Position.z = gl_Position.w - gl_Position.z;\\n"')
    biased = fixed.index('"\\tgl_Position.z += depth_bias * gl_Position.w;\\n"')
    assert reversed_ < biased < fixed.index(':\n\t\t\t"");')
    # (the uniform it reads is the prologue's, which the program is built with)
    assert '"uniform float depth_bias;\\n"' in source[:source.index("char *nv2a_fixed_function_glsl(void)")]
    assert 'xgpu_text_append(&text, "%s", shader_prologue);' in fixed
