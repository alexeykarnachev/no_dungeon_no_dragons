layout(location = 0) in vec3 a_cube_center_position;

uniform mat4 u_vp;
uniform float u_cube_size;

void main() {
    vec3 position;
    position = a_cube_center_position + vec3(
        -u_cube_size + mod(float(gl_VertexID), 3.0) * (2.0 * u_cube_size / 2.0),
        -u_cube_size + mod(floor(float(gl_VertexID) / 3.0), 3.0) * (2.0 * u_cube_size / 2.0),
        u_cube_size
    );

    gl_Position = u_vp * vec4(position, 1.0);
}
