uniform vec2 projScale;
uniform vec2 translation;
attribute vec2 position;
varying vec2 pos;

void main()
{
    gl_Position = vitaProject(vec4(position + translation, 0, 1), projScale);
    pos = position;
}
