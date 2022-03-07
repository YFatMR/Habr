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

enum class TaskStatus {
    in_q,
    completed
};


class Task {
public:
    template <typename FuncRetType, typename ...Args>
    Task(FuncRetType(*func)(Args...), Args... args) : is_void{ std::is_void_v<FuncRetType> } {
        if constexpr (std::is_void_v<FuncRetType>) {
            if constexpr (sizeof...(args)) {
                void_func = std::bind(func, args...);
            }
            else {
                void_func = func;
            }
            any_func = []()->int { return 0; };
        }
        else {
            void_func = []()->void {};
            any_func = std::bind(func, args...);
        }
    }

    Task operator= (const Task& other) {
        return Task(other);
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
    thread_pool(uint32_t num_threads) {
        threads.reserve(num_threads);
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back(std::thread(&thread_pool::run, this));
        }
    }

    template <typename Func, typename ...Args>
    bool add_task(const std::string label, const Func& func, Args&&... args) {
        std::unique_lock<std::mutex> lock(tasks_info_mtx);
        if (tasks_info.find(label) != tasks_info.end()) {
            std::cerr << "Can't add exist label key!\n";
            return false;
        }
        tasks_info[label] = TaskInfo();
        lock.unlock();

        std::lock_guard<std::mutex> q_lock(q_mtx);
        q.emplace(std::make_pair(Task(func, args...), label));
        q_cv.notify_one();
        return true;
    }

    void wait(const std::string& label) {
        std::unique_lock<std::mutex> lock(tasks_info_mtx);
        tasks_info_cv.wait(lock, [this, label]()->bool {return tasks_info.find(label) != tasks_info.end() && tasks_info[label].status == TaskStatus::completed; });
    }

    std::any wait_result(const std::string& label) {
        std::unique_lock<std::mutex> lock(tasks_info_mtx);
        tasks_info_cv.wait(lock, [this, label]()->bool {return tasks_info.find(label) != tasks_info.end() && tasks_info[label].status == TaskStatus::completed; });
        return tasks_info[label].result;
    }

    void wait_all() {
        std::unique_lock<std::mutex> lock(q_mtx);
        // And also check that all process not active
        wait_all_cv.wait(lock, [this]()->bool { return q.empty(); });
    }

    bool calculated(const std::string& label) {
        std::lock_guard<std::mutex> lock(tasks_info_mtx);
        return tasks_info.find(label) != tasks_info.end() && tasks_info[label].status == TaskStatus::completed;
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

            if (!q.empty()) {
                std::pair<Task, std::string> task = std::move(q.front());
                q.pop();
                lock.unlock();

                task.first();

                std::lock_guard<std::mutex> lock(tasks_info_mtx);
                if (task.first.has_result()) {
                    tasks_info[task.second].result = task.first.get_result();
                }
                tasks_info[task.second].status = TaskStatus::completed;
            }
            wait_all_cv.notify_all();
            tasks_info_cv.notify_all(); // notify for wait function
        }
    }

    

    std::queue<std::pair<Task, std::string>> q;
    std::mutex q_mtx;
    std::condition_variable q_cv;

    std::vector<std::thread> threads;

    std::atomic<bool> quite{ false };

    std::unordered_map<std::string, TaskInfo> tasks_info;
    std::condition_variable tasks_info_cv;
    std::mutex tasks_info_mtx;

    std::condition_variable wait_all_cv;
    
};


void sum(int a, int b) {
    std::cout << "sum\n";
    using namespace std::chrono_literals;
    //std::this_thread::sleep_for(2000ms);
    std::this_thread::sleep_for(1000ms);
    std::cout << "Success" << a + b << std::endl;
    //return a + b;
}

int sum2(int a, int b) {
    return a + b;
}

/*
* 
* Не передаётсв bind
template <typename R, typename Func, typename ...Args>
    bool add_task2(const std::string& label, const std::function<R(Args...)>& fn) {

        return true;
    }

*/

/*
std::variant<std::function<std::any()>, std::future<void>>

т к 

std::variant<std::function<std::any()>, std::function<void()>> yне работает

*/





int main() {
    // future - храним void 
    // bind - other
    //std::future<void> p = std::async(std::launch::deferred, sum, 2, 3);
    
    //std::invoke_result_t<decltype(sum),int, int> a;
    //a = 10;


    auto x = std::bind(sum, 3, 2);
    auto x2 = std::bind(sum2, 3, 2);

    
    thread_pool t(3);
    

    t.add_task("456", sum2, 100, 300);
    auto res = std::any_cast<int>(t.wait_result("456"));
    std::cout << res << std::endl;



    return 0;
}
