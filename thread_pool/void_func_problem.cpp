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


class thread_pool {
public:

    thread_pool(uint32_t num_threads) {
        threads.reserve(num_threads);
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back(std::thread(&thread_pool::run, this));
        }
    }

    template <typename Func, typename ...Args>
    bool add_task(const std::string& label, const Func& func, Args&&... args) {
        {
            std::lock_guard<std::mutex> lock(labels_mtx);
            if (labels.find(label) != labels.end()) {
                std::cerr << "Can't add exist label key!\n";
                return false;
            }
            labels.insert(label);
        }
        std::lock_guard<std::mutex> lock(q_mtx);
        q.push(std::make_pair(std::bind(func, args...), label));
        task_cv.notify_one();
        return true;
    }

    std::any wait(const std::string& label) {
        std::unique_lock<std::mutex> lock(um_mtx);
        um_cv.wait(lock, [this, label]()->bool {return um.find(label) != um.end(); });
        return um[label];
    }

    void wait_all() {
        std::unique_lock<std::mutex> lock(q_mtx);
        q_cv.wait(lock, [this]()->bool { return q.empty(); });
    }

    bool calculated(const std::string& label) {
        std::unique_lock<std::mutex> lock(um_mtx);
        if (um.find(label) != um.end()) {
            return true;
        }
        return false;
    }

    ~thread_pool() {
        quite = true;
        task_cv.notify_all();
        for (int i = 0; i < threads.size(); ++i) {
            threads[i].join();
        }
    }

private:

    void run() {
        while (!quite) {
            std::unique_lock<std::mutex> lock(task_mtx);
            task_cv.wait(lock, [this]()->bool {
                std::lock_guard<std::mutex> lock(q_mtx);
                return !q.empty() || quite;
                });

            if (!q.empty()) {
                auto func = std::move(q.front());
                q.pop();
                lock.unlock();
                auto res = func.first();

                std::lock_guard<std::mutex> lock(um_mtx);
                um[func.second] = res;
            }
            um_cv.notify_all();
            q_cv.notify_all();
        }
    }




    std::queue<std::pair<std::function<std::any()>, std::string>> q;
    std::queue<std::pair<std::function<void()>, std::string>> void_q;
    std::queue<int> order_q;

    std::mutex q_mtx;
    std::condition_variable q_cv;

    std::unordered_map<std::string, std::any> um;
    std::condition_variable um_cv;
    std::mutex um_mtx;
    std::vector<std::thread> threads;

    std::condition_variable task_cv;
    std::mutex task_mtx;

    std::atomic<bool> quite{ false };

    std::unordered_set<std::string> labels;
    std::mutex labels_mtx;
};


int sum(int a, int b) {
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(2000ms);
    return a + b;
}

int sum2(int& c, int a, int b) {
    c = a + b;
    return c;
}

int main() {



    thread_pool t(3);
    t.add_task("123", sum, 2, 3);
    t.add_task("234", sum, 100, 3);
    t.add_task("123123", sum, 2, 2);
    t.add_task("353142", sum, 2, 2);
    
    int c;
    t.add_task("12332634123", sum2, std::ref(c), 2, 2);
    //t.add_task("main", sum2, c, 100, 200);

    std::any res = t.wait("234");

    //t.add_task("main", sum2, 3, 2, 2);

    std::cout << std::any_cast<int>(res);

    t.wait("12332634123");
    std::cout << c;


    return 0;
}
