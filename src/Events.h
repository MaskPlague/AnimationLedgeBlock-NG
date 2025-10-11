#pragma once

namespace Events
{

    class CombatEventSink final : public RE::BSTEventSink<RE::TESCombatEvent>
    {
    public:
        virtual RE::BSEventNotifyControl ProcessEvent(
            const RE::TESCombatEvent *a_event,
            RE::BSTEventSource<RE::TESCombatEvent> *) override;
        static CombatEventSink *GetSingleton();
    };

    class AttackAnimationGraphEventSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::BSAnimationGraphEvent *event,
            RE::BSTEventSource<RE::BSAnimationGraphEvent> *) override;
        static AttackAnimationGraphEventSink *GetSingleton();
    };
}