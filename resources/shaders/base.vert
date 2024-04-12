in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

struct Camera {
    float view_width;
    float aspect;
    vec2 target;
};

uniform Camera camera;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragPosition;
out vec2 fragScreenPosition;

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragPosition = vertexPosition;

    float view_height = camera.view_width / camera.aspect;
    float x = (vertexPosition.x - camera.target.x) / (0.5 * camera.view_width);
    float y = (vertexPosition.y - camera.target.y) / (-0.5 * view_height);
    vec4 position = vec4(x, y, 0.0, 1.0);

    fragScreenPosition = (position.xy + 1.0) / 2.0;
    gl_Position = position;
}
