#pragma once
// Stub for the declaration parser: only the shape of std23::function_ref matters, never its behavior.

namespace std23 {
    template <class...>
    class function_ref;

    template <class R, class... A>
    class function_ref<R(A...)> {
    public:
        function_ref() = default;
        template <class F>
        function_ref(F&&) {}
        R operator()(A...) const;
        explicit operator bool() const noexcept;
    };

    template <class R, class... A>
    class function_ref<R(A...) const> : public function_ref<R(A...)> {
    public:
        using function_ref<R(A...)>::function_ref;
    };

    template <class R, class... A>
    class function_ref<R(A...) noexcept> : public function_ref<R(A...)> {
    public:
        using function_ref<R(A...)>::function_ref;
    };
}
