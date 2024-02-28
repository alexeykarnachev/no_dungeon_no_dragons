// left bot vertex position
layout(location = 0) in vec3 a_cube_position;
layout(location = 1) in float a_face_id;

uniform mat4 u_vp;
uniform float u_cube_size;

flat out int vs_face_id;

#define N 36
const vec3 OFFSETS[N] = vec3[N](
    // 0: bot
    vec3(0.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0),
    // 1: top
    vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0),
    vec3(0.0, 1.0, 1.0), vec3(1.0, 0.0, 1.0), vec3(1.0, 1.0, 1.0),
    // 2: front
    vec3(0.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0),
    vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 1.0),
    // 3: left
    vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0),
    // 4: right
    vec3(1.0, 0.0, 0.0), vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 1.0),
    vec3(1.0, 0.0, 1.0), vec3(1.0, 1.0, 0.0), vec3(1.0, 1.0, 1.0),
    // 5: back
    vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0),
    vec3(0.0, 1.0, 1.0), vec3(1.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0)
);

void main() {
    int face_id = int(a_face_id);
    int vertex_id = (gl_VertexID % 6) + face_id * 6;
    vec3 position = a_cube_position + OFFSETS[vertex_id] * u_cube_size;

    vs_face_id = face_id;
    gl_Position = u_vp * vec4(position, 1.0);
}
