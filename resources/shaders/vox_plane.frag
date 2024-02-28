flat in int vs_face_id;

out vec4 fs_color;

void main() {
    vec3 color = vec3(0.0, 0.0, 0.0);

    if (vs_face_id == 0) {
        color = vec3(1.0, 0.0, 0.0);
    } else if (vs_face_id == 1) {
        color = vec3(0.0, 1.0, 0.0);
    } else if (vs_face_id == 2) {
        color = vec3(0.0, 0.0, 1.0);
    } else if (vs_face_id == 3) {
        color = vec3(1.0, 1.0, 0.0);
    } else if (vs_face_id == 4) {
        color = vec3(1.0, 0.0, 1.0);
    } else if (vs_face_id == 5) {
        color = vec3(0.0, 1.0, 1.0);
    }

    fs_color = vec4(color, 1.0);
}
