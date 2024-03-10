in vec2 fragTexCoord;

out vec4 fs_color;

uniform sampler2D texture0;

void main() {
    vec2 tex_size = vec2(textureSize(texture0, 0));
    vec2 uv = fragTexCoord * tex_size;

    uv = floor(uv) + min(fract(uv) / fwidth(uv), 1.0) - 0.5;
    uv /= tex_size;

    fs_color = texture(texture0, uv);
}
