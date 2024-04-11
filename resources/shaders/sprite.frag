in vec2 fragTexCoord;
in vec3 fragPosition;
in vec2 fragScreenPosition;

out vec4 fs_color;

struct Light {
    float intensity;
    vec2 position;
    vec3 color;
    vec3 attenuation;
};

uniform int n_lights;
uniform Light lights[32];

uniform sampler2D texture0;
uniform sampler2D shadow_map;
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
    if (texture_color.a < 0.99) discard;

    float shadow = texture(shadow_map, fragScreenPosition).r;
    float plain_color_weight = plain_color.a;
    float texture_color_weight = 1.0 - plain_color_weight;

    vec3 total_light = vec3(0.0, 0.0, 0.0);
    for (int i = 0; i < n_lights; ++i) {
        Light light = lights[i];
        float dist = distance(light.position, fragPosition.xy);
        float attenuation = 1.0 / dot(light.attenuation, vec3(1.0, dist, dist * dist));
        float shadow_factor = 1.0 - shadow * 0.8;
        total_light += light.color * light.intensity * attenuation * shadow_factor;
    }

    vec3 color = plain_color_weight * plain_color.rgb;
    color += total_light * texture_color_weight * texture_color.rgb;

    fs_color = vec4(color, 1.0);
}
