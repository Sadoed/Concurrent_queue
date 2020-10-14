#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace pt {
/**
    \class Блокирующий, потокобезопасный контейнер вида очередь(FIFO).

    Контейнер потокобезопасен для множественной записи и множественного чтения.

    \tparam T - тип элемента хранения.
 */
template <class T>
class Concurrent_queue {
public:
    Concurrent_queue();

    Concurrent_queue(const Concurrent_queue&) = delete;
    Concurrent_queue& operator=(const Concurrent_queue&) = delete;
    Concurrent_queue(Concurrent_queue&&)                 = delete;
    Concurrent_queue& operator=(Concurrent_queue&&) = delete;

    ~Concurrent_queue() = default;

    /**
        \brief Метод добавления элемента в конец очереди.

        \param item - элемент, добавляемый в очередь.
     */
    void push(const T& item);

    /**
        \brief Блокирующий метод удаления и получения объекта из очереди.

        Исполняющий этот метод поток блокируется, если в очереди нет элементов.
        Поток разблокируется как только в очереди появится хотя бы один элемент.

        \return T - Первый элемент очереди.
     */
    T pop();

    /**
        \brief Блокирующий на определенное пользователем время метод удаления и
               получения объекта из очереди.

        Исполняющий этот метод поток блокируется на время, указанное
        пользователем, если в очереди нет элементов.
        Поток разблокируется как только в очереди появится хотя бы один элемент
        или если время блокировки истечет.


        \param time - время ожидания получения элементов в очереди.
        \return std::optional<T> - Возвращает элемент, если время ожидания
                                   валидно и элементы есть в очереди, иначе
                                   false.
     */
    std::optional<T> pop(const std::chrono::milliseconds& time);

    /**
        \brief Потокобезопасный метод очистки содержимого контейнера.

     */
    void clear() noexcept;

private:
    std::deque<T> deque_;

    /// Переменная для блокировки на pop(), если элементов в очереди нет
    std::condition_variable cv_;
    /// Мутекс для доступа к внутреннему контейнеру std::deque.
    mutable std::mutex mutex_;
};

template <typename T>
Concurrent_queue<T>::Concurrent_queue() : deque_{} {}

template <typename T>
void Concurrent_queue<T>::push(const T& item) {
    {
        std::unique_lock<std::mutex> lock{mutex_};
        deque_.push_back(item);
    }
    cv_.notify_one();  //< Элементы появились в очереди - оповещаем об этом,
                       // ожидающий поток.
}

template <typename T>
T Concurrent_queue<T>::pop() {
    T value;
    {
        std::unique_lock<std::mutex> cv_lock{mutex_};
        // Если элементов в очереди нет - cv_lock разблокируется, а поток
        // исполнения отправится в пул "ожидающих потоков". Поток "проснется",
        // когда cv_.notify_one() дойдет до него и "разбудит", а продолжит
        // исполнение, если предикат вернет true - иначе поток снова пойдет в
        // пул "ожидающих потоков".
        cv_.wait(cv_lock, [this] { return deque_.size(); });

        value = deque_.front();
        deque_.pop_front();
    }
    return value;
}

template <typename T>
std::optional<T> Concurrent_queue<T>::pop(
    const std::chrono::milliseconds& time) {
    T value;
    {
        std::unique_lock<std::mutex> cv_lock{mutex_};
        // Если элементов в очереди нет - cv_lock разблокируется, а поток
        // исполнения отправится в пул "ожидающих потоков". Поток "проснется",
        // когда cv_.notify_one() дойдет до него и "разбудит" или время ожидания
        // подойдет к концу. Если после "пробуждения" от cv_.notify_one()
        // предикат вернет - true, то исполнение потока продложится, иначе поток
        // заснет на оставшееся время time. Если поток пробудится по истечению
        // времени time, то исполнение потока
        // продолжится и пройдет проверка предиката. Если предикат вернет true
        // будет возвращен элемент, иначе возвратится false.
        if (!cv_.wait_for(cv_lock, time, [this] { return deque_.size(); })) {
            return {};
        }

        value = deque_.front();
        deque_.pop_front();
    }
    return value;
}

template <typename T>
void Concurrent_queue<T>::clear() noexcept {
    std::unique_lock<std::mutex> push_lock{mutex_};

    deque_.clear();
}

}  // namespace pt
