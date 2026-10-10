#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::async — 迷你协程运行时（C++20）
//
//   task<void>        惰性协程任务（对称转移，异常经 co_await 传播）
//   Scheduler         单线程事件循环：定时器优先队列，相当于迷你 asyncio
//   sleep_for         co_await 挂起指定毫秒，不阻塞循环线程
//
// 用法（相当于 asyncio.gather）:
//   async::Scheduler loop;
//   loop.spawn(song_a());
//   loop.spawn(song_b());
//   loop.run();   // 全部完成才返回；首个异常会中断循环并抛出
//
// 说明：定时器只能从循环内的协程注册（单线程模型，无跨线程唤醒）。
// ════════════════════════════════════════════════════════════════════════════

#include <chrono>
#include <coroutine>
#include <exception>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include "log.hpp"

namespace musicpp::async {

class Scheduler;

// ── task<void> ──────────────────────────────────────────────────────────────
class task {
public:
    struct promise_type {
        std::exception_ptr err;
        std::coroutine_handle<> continuation{};   // co_await 者（若有）

        task get_return_object() {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        auto final_suspend() noexcept {
            // 对称转移：完成时把控制权交还 co_await 者，无人等待则回循环
            struct Awaiter {
                std::coroutine_handle<> cont;
                bool await_ready() noexcept { return false; }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept {
                    return cont ? cont : std::noop_coroutine();
                }
                void await_resume() noexcept {}
            };
            return Awaiter{continuation};
        }
        void return_void() noexcept {}
        void unhandled_exception() { err = std::current_exception(); }
    };

    using handle_t = std::coroutine_handle<promise_type>;

    task() = default;
    explicit task(handle_t h) : h_(h) {}
    task(task&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    task& operator=(task&& o) noexcept {
        if (this != &o) { if (h_) h_.destroy(); h_ = std::exchange(o.h_, {}); }
        return *this;
    }
    task(const task&) = delete;
    task& operator=(const task&) = delete;
    ~task() { if (h_) h_.destroy(); }

    handle_t handle() const { return h_; }

    // co_await task：挂起当前协程，任务完成后恢复（异常随 await_resume 抛出）
    auto operator co_await() noexcept {
        struct Awaiter {
            handle_t h;
            bool await_ready() const noexcept { return !h || h.done(); }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
                h.promise().continuation = awaiting;
                return h;   // 对称转移：启动子任务
            }
            void await_resume() {
                if (h.promise().err) std::rethrow_exception(h.promise().err);
            }
        };
        return Awaiter{h_};
    }

private:
    handle_t h_{};
};

// ── Scheduler ───────────────────────────────────────────────────────────────
class Scheduler {
public:
    // 注册定时器：ms 毫秒后恢复协程 h（由 sleep_for 调用）
    void schedule(std::coroutine_handle<> h, double ms) {
        timers_.push({elapsed() + ms, next_seq_++, h});
    }

    // 派生后台任务（循环持有所有权）
    void spawn(task t) {
        auto h = t.handle();
        alive_.push_back(std::move(t));
        h.resume();   // 启动：跑至第一个挂起点
        if (h.done()) harvest(h);
    }

    // 事件循环：所有定时器耗尽（= 全部任务完成）才返回
    void run() {
        while (!timers_.empty()) {
            const auto [t, seq, h] = timers_.top();
            timers_.pop();
            std::this_thread::sleep_until(start_ + std::chrono::duration<double, std::milli>(t));
            h.resume();
            if (h.done()) harvest(h);
        }
        alive_.clear();
        if (first_err_) std::rethrow_exception(first_err_);
    }

    size_t pending() const { return timers_.size(); }

private:
    struct Timer {
        double at;                      // 相对循环启动的毫秒
        uint64_t seq;                   // 同时刻的稳定次序
        std::coroutine_handle<> h;
        bool operator<(const Timer& o) const {   // 优先队列默认大顶堆，反转
            if (at != o.at) return at > o.at;
            return seq > o.seq;
        }
    };

    const std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    std::priority_queue<Timer> timers_;
    std::vector<task> alive_;
    uint64_t next_seq_ = 0;
    std::exception_ptr first_err_;

    double elapsed() const {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - start_).count();
    }

    void harvest(std::coroutine_handle<> h) {
        // 只收 task 的 promise（Timer 里的句柄都是 sleep_for 挂起的 task）
        auto& p = std::coroutine_handle<task::promise_type>::from_address(h.address()).promise();
        if (p.err && !first_err_) first_err_ = p.err;
    }
};

// ── sleep_for：co_await 挂起 ms 毫秒 ────────────────────────────────────────
inline auto sleep_for(Scheduler& sched, double ms) {
    struct Awaiter {
        Scheduler& sched;
        double ms;
        bool await_ready() const noexcept { return ms <= 0; }
        void await_suspend(std::coroutine_handle<> h) noexcept { sched.schedule(h, ms); }
        void await_resume() noexcept {}
    };
    return Awaiter{sched, ms};
}

// ── channel<T>：协程队列（asyncio.Queue 同款）──────────────────────────────
// 生产者 push，消费者 co_await pop()：空队列时挂起，push 唤醒；
// close() 后 pop 返回 std::nullopt（流结束信号）。单线程循环内使用。
template <class T>
class channel {
public:
    void push(T v) {
        if (!waiters_.empty()) {
            Waiter w = waiters_.front();
            waiters_.pop();
            *w.out = std::move(v);
            w.h.resume();
        } else {
            items_.push(std::move(v));
        }
    }

    void close() {
        closed_ = true;
        while (!waiters_.empty()) {
            Waiter w = waiters_.front();
            waiters_.pop();
            w.h.resume();   // out 保持 nullopt → 消费者读到结束
        }
    }

    bool closed() const { return closed_; }

    // co_await pop() -> std::optional<T>（nullopt = 已关闭且取空）
    auto pop() {
        struct Awaiter {
            channel& ch;
            std::optional<T> result;

            bool await_ready() {
                if (!ch.items_.empty()) {
                    result = std::move(ch.items_.front());
                    ch.items_.pop();
                    return true;
                }
                return ch.closed_;   // 已关闭且空：result = nullopt
            }
            void await_suspend(std::coroutine_handle<> h) { ch.waiters_.push({h, &result}); }
            std::optional<T> await_resume() { return std::move(result); }
        };
        return Awaiter{*this};
    }

private:
    struct Waiter {
        std::coroutine_handle<> h;
        std::optional<T>* out;
    };
    std::queue<T> items_;
    std::queue<Waiter> waiters_;
    bool closed_ = false;
};

} // namespace musicpp::async
