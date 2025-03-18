#include "math.h"

#include <cmath>
namespace kpengine
{

}
// {
//     namespace math
//     {

//         constexpr float PerlinNoiseFade(float t)
//         {
//             return t * t * t * (6 * t * t - 15 * t + 10);
//         }

//         float PerlinNoise1D(float x)
//         {
//             int left = std::floor(x);
//             int right = left + 1;

//             float dist_left = x - left;
//             float dist_right = x - right;

//             int grad_left = 2 * permutation[left % 256] - 255;
//             int grad_right = 2 * permutation[right % 256] - 255;

//             float alpha = PerlinNoiseFade(dist_left);

//             float p_1 = grad_left * dist_left;
//             float p_2 = grad_right * dist_left;

//             return std::lerp(p_1, p_2, alpha);
//         }

//         float PerlinNoise2D(float x, float y)
//         {
//             int adjust_x = std::floor(x);
//             int adjust_y = std::floor(y);

//             Eigen::Vector2f origin{x, y};
//             Eigen::Vector2i lt{adjust_x, adjust_y};
//             Eigen::Vector2i rt{adjust_x + 1, adjust_y};
//             Eigen::Vector2i lb{adjust_x, adjust_y + 1};
//             Eigen::Vector2i rb{adjust_x + 1, adjust_y + 1};

//             int hash_lt = permutation[permutation[lt[0] % 256] + (lt[1] % 256)];
//             int hash_rt = permutation[permutation[rt[0] % 256] + (rt[1] % 256)];
//             int hash_lb = permutation[permutation[lb[0] % 256] + (lb[1] % 256)];
//             int hash_rb = permutation[permutation[rb[0] % 256] + (rb[1] % 256)];

//             Eigen::Vector2f grad_lt = grads[hash_lt & 0xb];
//             Eigen::Vector2f grad_rt = grads[hash_rt & 0xb];
//             Eigen::Vector2f grad_lb = grads[hash_lb & 0xb];
//             Eigen::Vector2f grad_rb = grads[hash_rb & 0xb];

//             Eigen::Vector2f dist_lt = origin - lt;
//             Eigen::Vector2f dist_rt = origin - rt;
//             Eigen::Vector2f dist_lb = origin - lb;
//             Eigen::Vector2f dist_rb = origin - rb;

//             float p_lt = grad_lt.dot(dist_lt);
//             float p_rt = grad_rt.dot(dist_rt);
//             float p_lb = grad_lb.dot(dist_lb);
//             float p_rb = grad_rb.dot(dist_rb);

//             float lerp1 = std::lerp(p_lt, p_rt, PerlinNoiseFade(x - adjust_x));
//             float lerp2 = std::lerp(p_lb, p_rb, PerlinNoiseFade(x - adjust_x));
//             return std::lerp(lerp1, lerp2, y - adjust_y);
//         }

//     }
// }