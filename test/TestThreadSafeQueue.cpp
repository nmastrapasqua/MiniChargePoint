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
// Test 7: timeout su coda vuota
// ------------------------------------------------------------
void test_try_pop_for_timeout() {
    ThreadSafeQueue<int> q;

    auto start = std::chrono::steady_clock::now();

    auto v = q.try_pop_for(std::chrono::milliseconds(200));

    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    ASSERT_FALSE(v.has_value());

    // verifica che abbia atteso almeno ~200ms (con tolleranza)
    ASSERT_TRUE(elapsed.count() >= 180);
}

// ------------------------------------------------------------
// Test 8: messaggio entro timeout
// ------------------------------------------------------------
class DelayedProducer : public Poco::Runnable {
public:
    DelayedProducer(ThreadSafeQueue<int>& q) : queue(q) {}

    void run() override {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        queue.push(99);
    }

private:
    ThreadSafeQueue<int>& queue;
};

void test_try_pop_for_success() {
    ThreadSafeQueue<int> q;

    DelayedProducer p(q);
    Poco::Thread t;

    t.start(p);

    auto v = q.try_pop_for(std::chrono::milliseconds(500));

    t.join();

    ASSERT_TRUE(v.has_value());
    ASSERT_EQ(*v, 99);
}


// ------------------------------------------------------------
// Test 9: close durante attesa
// ------------------------------------------------------------
class CloseQueueTask : public Poco::Runnable {
public:
    CloseQueueTask(ThreadSafeQueue<int>& q) : queue(q) {}

    void run() override {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        queue.close();
    }

private:
    ThreadSafeQueue<int>& queue;
};

void test_try_pop_for_close() {
    ThreadSafeQueue<int> q;

    CloseQueueTask closer(q);
    Poco::Thread t;

    t.start(closer);

    auto v = q.try_pop_for(std::chrono::milliseconds(500));

    t.join();

    ASSERT_FALSE(v.has_value());
    ASSERT_TRUE(q.isClosed());
}


// ------------------------------------------------------------
// Test 10: race push vs close
// ------------------------------------------------------------
class RacingProducer : public Poco::Runnable {
public:
    RacingProducer(ThreadSafeQueue<int>& q, std::atomic<int>& success)
        : queue(q), successCount(success) {}

    void run() override {
        for (int i = 0; i < 1000; ++i) {
            if (queue.push(i)) {
                successCount++;
            } else {
                // queue chiusa → stop
                break;
            }
        }
    }

private:
    ThreadSafeQueue<int>& queue;
    std::atomic<int>& successCount;
};

class RacingCloser : public Poco::Runnable {
public:
    RacingCloser(ThreadSafeQueue<int>& q) : queue(q) {}

    void run() override {
        // piccolo delay per creare race reale
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        queue.close();
    }

private:
    ThreadSafeQueue<int>& queue;
};

void test_race_push_vs_close() {
    ThreadSafeQueue<int> q;

    std::atomic<int> successCount{0};

    RacingProducer p1(q, successCount);
    RacingProducer p2(q, successCount);

    RacingCloser closer(q);

    Poco::Thread tp1, tp2, tc;

    tp1.start(p1);
    tp2.start(p2);
    tc.start(closer);

    tp1.join();
    tp2.join();
    tc.join();

    // verifica: queue deve essere chiusa
    ASSERT_TRUE(q.isClosed());

    // verifica: almeno qualche push è riuscito
    ASSERT_TRUE(successCount.load() > 0);

    // verifica: non più di 2000 (limite teorico)
    ASSERT_TRUE(successCount.load() <= 2000);
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
    runTest("timeout su coda vuota", test_try_pop_for_timeout);
    runTest("messaggio entro timeout", test_try_pop_for_success);
    runTest("close durante attesa", test_try_pop_for_close);
    runTest("race push vs close", test_race_push_vs_close);

    std::cout << "\nPassed: " << g_passed
              << " | Failed: " << g_failed << "\n";

    return g_failed == 0 ? 0 : 1;
}
