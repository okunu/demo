#version 300 es

out vec4 FragColor;
in vec2 v_texCoord;
uniform sampler2D texture_diffuse1;
void main()
{
    FragColor = texture(texture_diffuse1, v_texCoord);
}