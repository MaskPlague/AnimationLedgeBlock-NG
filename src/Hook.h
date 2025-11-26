// Taken from https://github.com/SeaSparrowOG/ConsiderateFollowers/
namespace Hook
{
    template <class T>
    class ISingleton
    {
    public:
        static T *GetSingleton()
        {
            static T singleton;
            return std::addressof(singleton);
        }

        ISingleton(const ISingleton &) = delete;
        ISingleton(ISingleton &&) = delete;
        ISingleton &operator=(const ISingleton &) = delete;
        ISingleton &operator=(ISingleton &&) = delete;

    protected:
        ISingleton() = default;
        ~ISingleton() = default;
    };

    bool Install();

    class PlayerUpdateListener : public ISingleton<PlayerUpdateListener>
    {
    public:
        bool Install();

    private:
        inline static void Thunk(RE::PlayerCharacter *a_this, float a_delta);
        inline static REL::Relocation<decltype(&Thunk)> _func;
        static constexpr std::size_t idx{0xAD};

        inline static float internalCounter{0.0f};
        inline static float timeBetweenChecks{0.011f};
        inline static float internalCleanCounter{0.0f};
        inline static float timeBetweenCleaning{10.0f};
        inline static bool running = false;
    };
}