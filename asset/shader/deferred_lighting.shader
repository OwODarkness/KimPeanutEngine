{
    "version": 1,
    "variants": [
        {"name": "bound", "defines": []},
        {"name": "ray_query", "defines": ["KP_RAY_QUERY 1"]}
    ],
    "shaders": [
        {
            "stage": "vertex",
            "format": "glsl",
            "file": "gbuffer_debug_view.vert",
            "entry": "main",
            "defines": []
        },
        {
            "stage": "fragment",
            "format": "glsl",
            "file": "deferred_lighting.frag",
            "entry": "main",
            "defines": []
        }
    ]
}
