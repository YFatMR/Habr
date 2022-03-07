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

#include <vector>
#include <chrono>

class thread_pool {
public:
    thread_pool(uint32_t num_threads) {
        threads.reserve(num_threads);
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back(std::thread(&thread_pool::run, this));
        }
    }

    template <typename Func, typename ...Args>
    int64_t add_task(const Func& func, Args&&... args) {
        int64_t current_idx = last_idx++;

        std::lock_guard<std::mutex> q_lock(q_mtx);
        q.emplace(std::make_pair(std::async(std::launch::deferred, func, args...), current_idx));
        q_cv.notify_one();
        return current_idx;
    }

    void wait(int64_t task_id) {
        std::unique_lock<std::mutex> lock(completed_task_ids_mtx);
        completed_task_ids_cv.wait(lock, [this, task_id]()->bool { return completed_task_ids.find(task_id) != completed_task_ids.end(); });
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
        for (int i = 0; i < threads.size(); ++i) {
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

    std::queue<std::pair<std::future<void>, int64_t>> q;
    std::mutex q_mtx;
    std::condition_variable q_cv;

    std::unordered_set<int64_t> completed_task_ids;
    std::condition_variable completed_task_ids_cv;
    std::mutex completed_task_ids_mtx;

    std::vector<std::thread> threads;


    std::atomic<bool> quite{ false };
    std::atomic<int64_t> last_idx = 0;
};


void sum(int& res, const std::vector<int>& arr1) {
    using namespace std::chrono_literals;
    //std::this_thread::sleep_for(2000ms);
    res = 0;
    for (int i = arr1.size() - 1; i >= 0; --i) {
        for (int j = 0; j < arr1.size(); ++j) {
            res += arr1[i] + arr1[j];
        }
    }
}


void trr() {
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(2s);
}


void thread_pool_test(std::vector<int> ans, std::vector<int> arr) {
    auto begin = std::chrono::high_resolution_clock::now();

    thread_pool t(6);
    for (int i = 0; i < ans.size(); ++i) {
        t.add_task(sum, std::ref(ans[i]), std::ref(arr));
    }
    t.wait_all();

    auto end = std::chrono::high_resolution_clock::now();
    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
}

void without_thread_test(std::vector<int> ans, std::vector<int> arr) {

    auto begin = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ans.size(); ++i) {
        sum(std::ref(ans[i]), std::ref(arr));
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
}

void raw_thread_test(std::vector<int> ans, std::vector<int> arr) {
    auto begin = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ans.size() / 6; ++i) {
        std::thread t1(sum, std::ref(ans[i]), std::ref(arr));
        std::thread t2(sum, std::ref(ans[i]), std::ref(arr));
        std::thread t3(sum, std::ref(ans[i]), std::ref(arr));
        std::thread t4(sum, std::ref(ans[i]), std::ref(arr));
        std::thread t5(sum, std::ref(ans[i]), std::ref(arr));
        std::thread t6(sum, std::ref(ans[i]), std::ref(arr));

        t1.join(); t2.join(); t3.join(); t4.join(); t5.join(); t6.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
}


int main() {
    std::vector<int> ans(24);
    std::vector<int> arr1(10000);
    //thread_pool_test(ans, arr1);//  52474
    without_thread_test(ans, arr1); // 83954
    //raw_thread_test(ans, arr1); // 62386


    return 0;
}
