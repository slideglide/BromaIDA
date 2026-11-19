#pragma once

namespace std23 {
    template <class...>
    class move_only_function;

    template <class R, class... A>
    class move_only_function<R(A...)> {
    public:
        move_only_function() = default;
        template <class F>
        move_only_function(F&&) {}
        R operator()(A...) const;
        explicit operator bool() const noexcept;
    };

    template <class R, class... A>
    class move_only_function<R(A...) const> : public move_only_function<R(A...)> {
    public:
        using move_only_function<R(A...)>::move_only_function;
    };

    template <class R, class... A>
    class move_only_function<R(A...) noexcept> : public move_only_function<R(A...)> {
    public:
        using move_only_function<R(A...)>::move_only_function;
    };
}
