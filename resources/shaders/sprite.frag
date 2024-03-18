in vec2 fragTexCoord;

out vec4 fs_color;

uniform sampler2D texture0;

vec4 texture2DAA(sampler2D tex, vec2 uv) {
    vec2 texsize = vec2(textureSize(tex,0));
    vec2 uv_texspace = uv*texsize;
    vec2 seam = floor(uv_texspace+.5);
    uv_texspace = (uv_texspace-seam)/fwidth(uv_texspace)+seam;
    uv_texspace = clamp(uv_texspace, seam-.5, seam+.5);
    return texture(tex, uv_texspace/texsize);
}

void main() {
    vec2 tex_size = vec2(textureSize(texture0, 0));
    fs_color = texture2DAA(texture0, fragTexCoord);
    // vec2 uv = fragTexCoord * tex_size;

    // uv = floor(uv) + min(fract(uv) / fwidth(uv), 1.0) - 0.5;
    // uv /= tex_size;

    // fs_color = texture(texture0, uv);
}
