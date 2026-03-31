#include "TestHarness.h"
#include "common/ThreadSafeQueue.h"

#include <Poco/Thread.h>
#include <Poco/Runnable.h>
#include <vector>
#include <atomic>
#include <chrono>

// ------------------------------------------------------------
// Test 1: push + pop base
// ------------------------------------------------------------
void test_basic_push_pop() {
    ThreadSafeQueue<int> q;

    q.push(42);
    auto v = q.pop();

    ASSERT_TRUE(v.has_value());
    ASSERT_EQ(*v, 42);
}

// ------------------------------------------------------------
// Test 2: try_pop su coda vuota
// ------------------------------------------------------------
void test_try_pop_empty() {
    ThreadSafeQueue<int> q;

    auto v = q.try_pop();

    ASSERT_FALSE(v.has_value());
}

// ------------------------------------------------------------
// Test 3: close + pop
// ------------------------------------------------------------
void test_close_behavior() {
    ThreadSafeQueue<int> q;

    q.close();
    auto v = q.pop();

    ASSERT_FALSE(v.has_value());
}

// ------------------------------------------------------------
// Test 4: push dopo close
// ------------------------------------------------------------
void test_push_after_close() {
    ThreadSafeQueue<int> q;

    q.close();
    bool ok = q.push(10);

    ASSERT_FALSE(ok);
}

// ------------------------------------------------------------
// Worker POCO per test concorrente
// ------------------------------------------------------------
class Producer : public Poco::Runnable {
public:
    Producer(ThreadSafeQueue<int>& q, int start, int count)
        : queue(q), startVal(start), countVal(count) {}

    void run() override {
        for (int i = 0; i < countVal; ++i) {
            queue.push(startVal + i);
        }
    }

private:
    ThreadSafeQueue<int>& queue;
    int startVal;
    int countVal;
};

class Consumer : public Poco::Runnable {
public:
    Consumer(ThreadSafeQueue<int>& q, std::atomic<int>& counter)
        : queue(q), consumed(counter) {}

    void run() override {
        while (true) {
            auto v = queue.pop();
            if (!v) break;
            consumed++;
        }
    }

private:
    ThreadSafeQueue<int>& queue;
    std::atomic<int>& consumed;
};

// ------------------------------------------------------------
// Test 5: concorrenza producer/consumer
// ------------------------------------------------------------
void test_multithread() {
    ThreadSafeQueue<int> q;

    const int N = 1000;

    std::atomic<int> consumed{0};

    Producer p1(q, 0, N);
    Producer p2(q, N, N);

    Consumer c1(q, consumed);
    Consumer c2(q, consumed);

    Poco::Thread tp1, tp2, tc1, tc2;

    tp1.start(p1);
    tp2.start(p2);
    tc1.start(c1);
    tc2.start(c2);

    tp1.join();
    tp2.join();

    // chiudi la coda → sblocca consumer
    q.close();

    tc1.join();
    tc2.join();

    ASSERT_EQ(consumed.load(), 2 * N);
}

// ------------------------------------------------------------
// Test 6: pop bloccante + close
// ------------------------------------------------------------
class BlockingConsumer : public Poco::Runnable {
public:
    BlockingConsumer(ThreadSafeQueue<int>& q, std::atomic<bool>& done)
        : queue(q), finished(done) {}

    void run() override {
        auto v = queue.pop();
        if (!v) {
            finished = true;
        }
    }

private:
    ThreadSafeQueue<int>& queue;
    std::atomic<bool>& finished;
};

void test_blocking_pop_unblocks_on_close() {
    ThreadSafeQueue<int> q;
    std::atomic<bool> done{false};

    BlockingConsumer c(q, done);
    Poco::Thread t;

    t.start(c);

    // aspetta un attimo per essere sicuri che sia bloccato
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    q.close();
    t.join();

    ASSERT_TRUE(done.load());
}

// ------------------------------------------------------------
// MAIN
// ------------------------------------------------------------
int main() {
    std::cout << "Running ThreadSafeQueue tests...\n";

    runTest("basic push/pop", test_basic_push_pop);
    runTest("try_pop empty", test_try_pop_empty);
    runTest("close behavior", test_close_behavior);
    runTest("push after close", test_push_after_close);
    runTest("multithread producer/consumer", test_multithread);
    runTest("blocking pop unblocks on close", test_blocking_pop_unblocks_on_close);

    std::cout << "\nPassed: " << g_passed
              << " | Failed: " << g_failed << "\n";

    return g_failed == 0 ? 0 : 1;
}
