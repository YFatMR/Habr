#include <iostream>
#include <queue>
#include <thread>
#include <chrono>
#include <mutex>
#include <future>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <any>
#include <atomic>
#include <variant>
#include <cassert>
#include <map>
#include <utility>

enum class TaskStatus {
    in_q,
    completed
};


// C++ 17
class Task {
public:
    template <typename FuncRetType, typename ...Args, typename ...FuncTypes>
    Task(FuncRetType(*func)(FuncTypes...), Args&&... args) :
        is_void{ std::is_void_v<FuncRetType> } {

        if constexpr (std::is_void_v<FuncRetType>) {
            void_func = std::bind(func, args...);
            any_func = []()->int { return 0; };
        }
        else {
            void_func = []()->void {};
            any_func = std::bind(func, args...);
        }
    }

    void operator() () {
        void_func();
        any_func_result = any_func();
    }

    bool has_result() {
        return !is_void;
    }

    std::any get_result() const {
        assert(!is_void);
        assert(any_func_result.has_value());
        return any_func_result;
    }

private:
    std::function<void()> void_func;
    std::function<std::any()> any_func;
    std::any any_func_result;
    bool is_void;
};

struct TaskInfo {
    TaskStatus status = TaskStatus::in_q;
    std::any result;
};


class thread_pool {
public:
    thread_pool(const uint32_t num_threads) {
        threads.reserve(num_threads);
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back(&thread_pool::run, this);
        }
    }

    template <typename Func, typename ...Args, typename ...FuncTypes>
    uint64_t add_task(Func(*func)(FuncTypes...), Args&&... args) {

        const uint64_t task_id = last_idx++;

        std::unique_lock<std::mutex> lock(tasks_info_mtx);
        tasks_info[task_id] = TaskInfo();
        lock.unlock();

        std::lock_guard<std::mutex> q_lock(q_mtx);
        q.emplace(Task(func, std::forward<Args>(args)...), task_id);
        q_cv.notify_one();
        return task_id;
    }

    void wait(const uint64_t task_id) {
        std::unique_lock<std::mutex> lock(tasks_info_mtx);
        tasks_info_cv.wait(lock, [this, task_id]()->bool {
            return task_id < last_idx&& tasks_info[task_id].status == TaskStatus::completed;
            });
    }

    std::any wait_result(const uint64_t task_id) {
        std::unique_lock<std::mutex> lock(tasks_info_mtx);
        wait(task_id);
        return tasks_info[task_id].result;
    }

    template<class T>
    void wait_result(const uint64_t task_id, T& value) {
        std::unique_lock<std::mutex> lock(tasks_info_mtx);
        wait(task_id);
        value = std::any_cast<T>(tasks_info[task_id].result);
    }

    void wait_all() {
        std::unique_lock<std::mutex> lock(tasks_info_mtx);
        wait_all_cv.wait(lock, [this]()->bool { return cnt_completed_tasks == last_idx; });
    }

    bool calculated(const uint64_t task_id) {
        std::lock_guard<std::mutex> lock(tasks_info_mtx);
        return task_id < last_idx&& tasks_info[task_id].status == TaskStatus::completed;
    }

    ~thread_pool() {
        quite = true;
        q_cv.notify_all();
        for (int i = 0; i < threads.size(); ++i) {
            threads[i].join();
        }
    }

private:

    void run() {
        while (!quite) {
            std::unique_lock<std::mutex> lock(q_mtx);
            q_cv.wait(lock, [this]()->bool { return !q.empty() || quite; });

            if (!q.empty() && !quite) {
                std::pair<Task, uint64_t> task = std::move(q.front());
                q.pop();
                lock.unlock();

                task.first();

                std::lock_guard<std::mutex> lock(tasks_info_mtx);
                if (task.first.has_result()) {
                    tasks_info[task.second].result = task.first.get_result();
                }
                tasks_info[task.second].status = TaskStatus::completed;
                ++cnt_completed_tasks;
            }
            wait_all_cv.notify_all();
            tasks_info_cv.notify_all(); // notify for wait function
        }
    }

    std::vector<std::thread> threads;

    std::queue<std::pair<Task, uint64_t>> q;
    std::mutex q_mtx;
    std::condition_variable q_cv;

    std::unordered_map<uint64_t, TaskInfo> tasks_info;
    std::condition_variable tasks_info_cv;
    std::mutex tasks_info_mtx;

    std::condition_variable wait_all_cv;

    std::atomic<bool> quite{ false };
    std::atomic<uint64_t> last_idx{ 0 };
    std::atomic<uint64_t> cnt_completed_tasks{ 0 };
};

int int_sum(int a, int b) {
    return a + b;
}

void void_sum(int& c, int a, int b) {
    c = a + b;
}

void void_without_argument() {
    std::cout << "It's OK!" << std::endl;
}

int main() {
    thread_pool t(3);
    int c;
    t.add_task(int_sum, 2, 3);               // id = 0
    t.add_task(void_sum, std::ref(c), 4, 6); // id = 1
    t.add_task(void_without_argument);       // id = 2

    {
        // variant 1
        int res;
        t.wait_result(0, res);
        std::cout << res << std::endl;

        // variant 2
        std::cout << std::any_cast<int>(t.wait_result(0)) << std::endl;
    }

    t.wait(1);
    std::cout << c << std::endl;

    t.wait_all(); // waiting for task with id 2

    return 0;
}
