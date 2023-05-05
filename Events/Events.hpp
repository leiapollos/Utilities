#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <queue>

class Event {
public:
    enum class Type { // Examples
        InputKeyPress,
        InputKeyRelease,
        InputMouseMove,
        InputMouseButtonPress,
        InputMouseButtonRelease,
        EntityCreated,
        EntityDestroyed,
        EntityCollision,
        LevelLoaded,
        LevelUnloaded,
        GameStarted,
        GamePaused,
        GameResumed,
        GameOver,
        PlayerScoreChanged,
        PlayerHealthChanged,
        SoundPlay,
        SoundStop,
        MusicPlay,
        MusicStop,
    };

    explicit Event(Type _type) : _type(_type) {}

    Type getType() const { return _type; }

    std::string cenas;
    int i;
private:
    Type _type;
};


class EventManager {
public:
    using ListenerID = std::size_t;
    using EventCallback = std::function<void(const Event&)>;

    EventManager();
    ~EventManager();

    ListenerID addListener(Event::Type eventType, EventCallback callback);
    void removeListener(Event::Type eventType, ListenerID listenerID);
    void fireEvent(const Event& event);

private:
    void workerThreadFunction(std::stop_token stopToken);

    std::unordered_map<Event::Type, std::vector<EventCallback>> _listeners;
    std::unordered_map<Event::Type, std::shared_mutex> _listenerLocks;
    std::unordered_map<Event::Type, std::unordered_map<ListenerID, size_t>> _listenerIndices;
    ListenerID _nextListenerID = 0;

    std::queue<Event> _eventQueue;
    std::mutex _eventQueueMutex;
    std::condition_variable_any _eventQueueCV;
    std::vector<std::jthread> _workerThreads;
    std::stop_source _stopSource;
};
