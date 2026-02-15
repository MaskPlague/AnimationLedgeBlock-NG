namespace MathUtils
{
    float AverageAngles(const std::vector<float> &angles)
    {
        float x = 0.0f;
        float y = 0.0f;

        for (const float angle : angles)
        {
            x += std::cos(angle);
            y += std::sin(angle);
        }

        // Handle empty vector
        if (x == 0.0f && y == 0.0f)
            return 0.0f;

        return std::atan2(y, x);
    }

    float NormalizeAngle(const float angle)
    {
        return std::fmod((angle + 2 * RE::NI_PI), (2 * RE::NI_PI));
    }

    bool IsMaxMinZPastDropThreshold(const std::vector<float> hitZ, const std::vector<float> opHitZ, float actor_z)
    {
        if (hitZ.empty() || opHitZ.empty())
            return false;
        float max = opHitZ[0];
        for (const float z : opHitZ)
        {
            max = std::max(z, max);
        }
        max = std::min(max, actor_z);
        if (max <= actor_z - Globals::ground_leeway)
            return false;
        float min = hitZ[0];
        for (const float z : hitZ)
        {
            min = std::min(min, z);
        }
        auto diff = max - min;
        // logger::trace("Diff {}"sv, diff);
        if (diff >= Globals::drop_threshold)
            return true;
        else
            return false;
    }
}