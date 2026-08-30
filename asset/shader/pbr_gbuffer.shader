{
    "version": 1,
    "variants": [
        {"name": "bound", "defines": ["KP_USE_BINDLESS 0"]}
    ],
    "shaders": [
        {
            "stage": "vertex",
            "format": "glsl",
            "file": "pbr_gbuffer.vert",
            "entry": "main",
            "defines": []
        },
        {
            "stage": "fragment",
            "format": "glsl",
            "file": "pbr_gbuffer.frag",
            "entry": "main",
            "defines": []
        }
    ]
}
