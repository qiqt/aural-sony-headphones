#pragma once

#include <chrono>
#include <future>
#include <stdexcept>
#include <utility>

// One operation at a time. Its result must be consumed before the next launch.
template<class T>
class AsyncTask
{
public:
    bool valid() const noexcept { return _result.valid(); }

    bool ready() const
    {
        return valid() && _result.wait_for(std::chrono::milliseconds::zero()) == std::future_status::ready;
    }

    template<class Function, class... Arguments>
    void setFromAsync(Function&& function, Arguments&&... arguments)
    {
        if (valid()) throw std::logic_error("An operation is already pending");
        _result = std::async(std::launch::async,
            std::forward<Function>(function), std::forward<Arguments>(arguments)...);
    }

    T get() { return _result.get(); }

private:
    std::future<T> _result;
};
