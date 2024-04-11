in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragPosition;
out vec2 fragScreenPosition;

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragPosition = vertexPosition;

    vec4 position = mvp * vec4(vertexPosition, 1.0);
    fragScreenPosition = ((position.xy / position.z) + 1.0) / 2.0;
    gl_Position = position;
}
