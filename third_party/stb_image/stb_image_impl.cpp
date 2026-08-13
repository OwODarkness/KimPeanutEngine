// Single TU owning the stb_image implementation. Consumers include
// <stb_image/stb_image.h> for declarations only; defining the implementation
// here compiles the library exactly once (avoids LNK2005 across TUs).
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image/stb_image.h>
