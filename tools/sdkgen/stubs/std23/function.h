#pragma once

namespace std23 {
    template <class...>
    class function;

    template <class R, class... A>
    class function<R(A...)> {
    public:
        function() = default;
        template <class F>
        function(F&&) {}
        R operator()(A...) const;
        explicit operator bool() const noexcept;
    };

    template <class R, class... A>
    class function<R(A...) const> : public function<R(A...)> {
    public:
        using function<R(A...)>::function;
    };

    template <class R, class... A>
    class function<R(A...) noexcept> : public function<R(A...)> {
    public:
        using function<R(A...)>::function;
    };
}
