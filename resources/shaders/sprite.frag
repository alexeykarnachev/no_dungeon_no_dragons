in vec2 fragTexCoord;

out vec4 fs_color;

uniform sampler2D texture0;
uniform vec4 plain_color;

vec4 texture2DAA(sampler2D tex, vec2 uv) {
    vec2 texsize = vec2(textureSize(tex,0));
    vec2 uv_texspace = uv*texsize;
    vec2 seam = floor(uv_texspace+.5);
    uv_texspace = (uv_texspace-seam)/fwidth(uv_texspace)+seam;
    uv_texspace = clamp(uv_texspace, seam-.5, seam+.5);
    return texture(tex, uv_texspace/texsize);
}

void main() {
    vec4 texture_color = texture2DAA(texture0, fragTexCoord);
    if (texture_color.a < 0.9) discard;

    float plain_color_weight = plain_color.a;
    float texture_color_weight = 1.0 - plain_color_weight;

    fs_color = texture_color_weight * texture_color + plain_color_weight * plain_color;
}
