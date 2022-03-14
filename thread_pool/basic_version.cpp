#include <iostream>
#include <queue>
#include <thread>
#include <chrono>
#include <mutex>
#include <future>
#include <unordered_set>
#include <atomic>

#include <vector>
#include <chrono>

// C++ 14
class thread_pool {
public:
    thread_pool(uint32_t num_threads) {
        threads.reserve(num_threads);
        for (uint32_t i = 0; i < num_threads; ++i) {
            threads.emplace_back(&thread_pool::run, this);
        }
    }

    template <typename Func, typename ...Args>
    int64_t add_task(const Func& task_func, Args&&... args) {
        int64_t task_idx = last_idx++;

        std::lock_guard<std::mutex> q_lock(q_mtx);
        q.emplace(std::async(std::launch::deferred, task_func, args...), task_idx);
        q_cv.notify_one();
        return task_idx;
    }

    void wait(int64_t task_id) {
        std::unique_lock<std::mutex> lock(completed_task_ids_mtx);
        completed_task_ids_cv.wait(lock, [this, task_id]()->bool {
            return completed_task_ids.find(task_id) != completed_task_ids.end();
            });
    }

    void wait_all() {
        std::unique_lock<std::mutex> lock(q_mtx);
        completed_task_ids_cv.wait(lock, [this]()->bool {
            std::lock_guard<std::mutex> task_lock(completed_task_ids_mtx);
            return q.empty() && last_idx == completed_task_ids.size();
            });
    }

    bool calculated(int64_t task_id) {
        std::lock_guard<std::mutex> lock(completed_task_ids_mtx);
        if (completed_task_ids.find(task_id) != completed_task_ids.end()) {
            return true;
        }
        return false;
    }

    ~thread_pool() {
        quite = true;
        for (uint32_t i = 0; i < threads.size(); ++i) {
            q_cv.notify_all();
            threads[i].join();
        }
    }

private:

    void run() {
        while (!quite) {
            std::unique_lock<std::mutex> lock(q_mtx);
            q_cv.wait(lock, [this]()->bool { return !q.empty() || quite; });

            if (!q.empty()) {
                auto elem = std::move(q.front());
                q.pop();
                lock.unlock();

                elem.first.get();

                std::lock_guard<std::mutex> lock(completed_task_ids_mtx);
                completed_task_ids.insert(elem.second);

                completed_task_ids_cv.notify_all();
            }
        }
    }

    std::queue<std::pair<std::future<void>, int64_t>> q; // очередь задач - хранит функцию(задачу), которую нужно исполнить и номер данной задачи
    std::mutex q_mtx;
    std::condition_variable q_cv;

    std::unordered_set<int64_t> completed_task_ids;      // помещаем в данный контейнер исполненные задачи
    std::condition_variable completed_task_ids_cv;
    std::mutex completed_task_ids_mtx;

    std::vector<std::thread> threads;


    std::atomic<bool> quite{ false };                    // флаг завершения работы thread_pool
    std::atomic<int64_t> last_idx = 0;                   // переменная хранящая id который будет выдан следующей задаче
};


void test_func(int& res, const std::vector<int>& arr) {
    using namespace std::chrono_literals;
    res = 0;
    for (int i = arr.size() - 1; i >= 0; --i) {
        for (int j = 0; j < arr.size(); ++j) {
            res += arr[i] + arr[j];
        }
    }
}


void thread_pool_test(std::vector<int> ans, std::vector<int> arr) {
    auto begin = std::chrono::high_resolution_clock::now();

    thread_pool t(6);
    for (int i = 0; i < ans.size(); ++i) {
        t.add_task(test_func, std::ref(ans[i]), std::ref(arr));
    }
    t.wait_all();

    auto end = std::chrono::high_resolution_clock::now();
    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
}

void without_thread_test(std::vector<int> ans, std::vector<int> arr) {

    auto begin = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ans.size(); ++i) {
        test_func(std::ref(ans[i]), std::ref(arr));
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
}

void raw_thread_test(std::vector<int> ans, std::vector<int> arr) {
    auto begin = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ans.size() / 6; ++i) {
        std::thread t1(test_func, std::ref(ans[i]), std::ref(arr));
        std::thread t2(test_func, std::ref(ans[i]), std::ref(arr));
        std::thread t3(test_func, std::ref(ans[i]), std::ref(arr));
        std::thread t4(test_func, std::ref(ans[i]), std::ref(arr));
        std::thread t5(test_func, std::ref(ans[i]), std::ref(arr));
        std::thread t6(test_func, std::ref(ans[i]), std::ref(arr));

        t1.join(); t2.join(); t3.join(); t4.join(); t5.join(); t6.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
}

void run_test() {
    std::vector<int> ans(24);
    std::vector<int> arr1(10000);
    thread_pool_test(ans, arr1);//  52474
    without_thread_test(ans, arr1); // 83954
    raw_thread_test(ans, arr1); // 62386
}

class Test {
public:
    void operator() () {
        std::cout << "Working with functors!\n";
    }
};

void sum(int a, int b) {
    std::cout << a + b << std::endl;
}

int main() {
    //run_test();

    Test test;
    auto res = std::bind(sum, 2, 3);

    thread_pool t(3);
    t.add_task(test);
    t.add_task(res);
    t.wait_all();

    return 0;
}
