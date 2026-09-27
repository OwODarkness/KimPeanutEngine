{
    "version": 1,
    "shaders": [
        {"stage": "raygen", "format": "glsl", "file": "ray_tracing_path_tracer.rgen", "entry": "main", "defines": []},
        {"stage": "miss", "format": "glsl", "file": "ray_tracing_path_tracer.rmiss", "entry": "main", "defines": []},
        {"stage": "visibility_miss", "format": "glsl", "file": "ray_tracing_visibility.rmiss", "entry": "main", "defines": []},
        {"stage": "closest_hit", "format": "glsl", "file": "ray_tracing_path_tracer.rchit", "entry": "main", "defines": []}
    ]
}
