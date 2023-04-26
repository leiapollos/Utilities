#include "Events.hpp"

static const size_t k_numWorkerThreads = 2;

EventManager::EventManager() {
    _workerThreads.reserve(k_numWorkerThreads);
    for (size_t i = 0; i < k_numWorkerThreads; ++i) {
        _workerThreads.emplace_back(&EventManager::workerThreadFunction, this);
    }
}

EventManager::~EventManager() {
    {
        std::unique_lock<std::mutex> lock(_eventQueueMutex);
        _stopWorkers = true;
    }
    _eventQueueCV.notify_all();
    for (auto& thread : _workerThreads) {
        thread.join();
    }
}

void EventManager::fireEvent(const Event& event) {
    {
        std::unique_lock<std::mutex> lock(_eventQueueMutex);
        _eventQueue.push(event);
    }
    _eventQueueCV.notify_one();
}

void EventManager::workerThreadFunction() {
    std::unique_lock<std::mutex> lock(_eventQueueMutex, std::defer_lock);
    while (true) {
        lock.lock();
        _eventQueueCV.wait(lock, [this]() { return !_eventQueue.empty() || _stopWorkers; });
        if (_stopWorkers && _eventQueue.empty()) {
            break;
        }
        auto eventToProcess = std::move(_eventQueue.front());
        _eventQueue.pop();
        lock.unlock();

        Event::Type eventType = eventToProcess.getType();
        {
            std::shared_lock<std::shared_mutex> lock(_listenerLocks[eventType]);
            auto& listenersForType = _listeners[eventType];
            for (const auto& listener : listenersForType) {
                listener(eventToProcess);
            }
        }
    }
}

EventManager::ListenerID EventManager::addListener(Event::Type eventType, EventCallback callback) {
    ListenerID listenerID = _nextListenerID++;
    {
        std::unique_lock<std::shared_mutex> lock(_listenerLocks[eventType]);
        size_t index = _listeners[eventType].size();
        _listeners[eventType].emplace_back(std::move(callback));
        _listenerIndices[eventType].emplace(listenerID, index);
    }
    return listenerID;
}

void EventManager::removeListener(Event::Type eventType, ListenerID listenerID) {
    std::unique_lock<std::shared_mutex> lock(_listenerLocks[eventType]);

    auto& listenersForType = _listeners[eventType];
    auto& indices = _listenerIndices[eventType];
    size_t index = indices[listenerID];

    // Fast "erase"
    std::swap(listenersForType[index], listenersForType.back());
    listenersForType.pop_back();

    // Update the index of the listener that was swapped
    for (const auto& pair : indices) {
        if (pair.second == listenersForType.size()) {
            indices[pair.first] = index;
            break;
        }
    }

    indices.erase(listenerID);
}
