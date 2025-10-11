#pragma once

namespace MathUtils
{
    float AverageAngles(const std::vector<float> &angles);

    float NormalizeAngle(const float angle);

    bool IsMaxMinZPastDropThreshold(const std::vector<float> hitZ, const std::vector<float> opHitZ, float actor_z);
}