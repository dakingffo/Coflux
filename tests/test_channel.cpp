#include <gtest/gtest.h>
#include <coflux/channel.hpp>
#include <coflux/task.hpp>
#include <coflux/scheduler.hpp>
#include <coflux/executor.hpp>
#include <coflux/combiner.hpp>
#include <coflux/this_coroutine.hpp> // for yield/sleep
#include <vector>
#include <thread>
#include <numeric>
/* testing...
using namespace coflux;

// 使用默认线程池
using pool = thread_pool_executor<>;
using sche = scheduler<pool>;

// --- 1. SPSC 基础测试 (非阻塞模式) ---
TEST(ChannelTest, BasicSPSC) {
    auto env = make_environment(sche{});

    auto test = [](auto env) -> task<void, pool> {
        channel<int[8]> chan; // 有界通道 (非阻塞)

        // 生产者
        auto producer = [](auto&&, channel<int[8]>& chan) -> fork<void, pool> {
            for (int i = 0; i < 100; ++i) {
                // 自旋重试直到写入成功
                while (!co_await(chan << i)) {
                    std::this_thread::yield(); // 或者 co_await this_fork::yield();
                }
            }
            }(co_await context(), chan);

        // 消费者
        auto consumer = [](auto&&, channel<int[8]>& chan) -> fork<void, pool> {
            int val;
            for (int i = 0; i < 100; ++i) {
                // 自旋重试直到读取成功
                while (!co_await(chan >> val)) {
                    std::this_thread::yield();
                }
                EXPECT_EQ(val, i);
            }
            }(co_await context(), chan);

        co_await when_all(producer, consumer);
        }(env);

    test.join();
}

// --- 2. MPMC 高并发测试 (非阻塞压力测试) ---
TEST(ChannelTest, ConcurrentMPMC) {
    auto env = make_environment(sche{});

    auto test = [](auto env) -> task<void, pool> {
        // 容量较小，强制触发满/空状态
        channel<int[32]> chan;
        const int N_PRODUCERS = 4;
        const int N_CONSUMERS = 4;
        const int ITEMS_PER_PRODUCER = 10000;

        std::atomic<int> total_consumed = 0;

        // 生产者任务
        auto make_producer = [&chan](auto&&, int id) -> fork<void, pool> {
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                int val = id * 100000 + i;
                // 遇到满队列则自旋重试
                while (!co_await(chan << val)) {
                    std::this_thread::yield();
                }
            }
            };

        // 消费者任务
        auto make_consumer = [&chan, &total_consumed](auto&&) -> fork<void, pool> {
            int val;
            // 消费定额数据
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                // 遇到空队列则自旋重试
                while (!co_await(chan >> val)) {
                    std::this_thread::yield();
                }
                total_consumed.fetch_add(1, std::memory_order_relaxed);
            }
            };

        std::vector<fork<void, pool>> producers;
        std::vector<fork<void, pool>> consumers;

        auto&& ctx = co_await context();
        for (int i = 0; i < N_PRODUCERS; ++i) producers.push_back(make_producer(ctx, i));
        for (int i = 0; i < N_CONSUMERS; ++i) consumers.push_back(make_consumer(ctx));

        // 等待所有任务完成
        for (auto& p : producers) co_await p;
        for (auto& c : consumers) co_await c;

        EXPECT_EQ(total_consumed.load(), N_PRODUCERS * ITEMS_PER_PRODUCER);

        }(env);

    test.join();
}

// --- 3. 非阻塞语义测试  ---
TEST(ChannelTest, NonBlockingSemantics) {
    auto env = make_environment(sche{});

    auto test = [](auto env) -> task<void, pool> {
        channel<int[2]> chan; // 容量为 2

        // 填满通道
        EXPECT_TRUE(co_await(chan << 1));
        EXPECT_TRUE(co_await(chan << 1));

        // 再次写入应立即失败 (返回 false)，而不是挂起
        EXPECT_FALSE(co_await(chan << 2));

        // 读取数据
        int val;
        EXPECT_TRUE(co_await(chan >> val));
        EXPECT_EQ(val, 1);
        EXPECT_TRUE(co_await(chan >> val));
        EXPECT_EQ(val, 1);

        // 再次读取应立即失败 (返回 false)，而不是挂起
        EXPECT_FALSE(co_await(chan >> val));

        }(env);

    test.join();
}


// --- 4. 关闭通道测试 ---
TEST(ChannelTest, CloseChannel) {
    auto env = make_environment(sche{});

    auto test = [](auto env) -> task<void, pool> {
        channel<int[16]> chan;

        // 消费者
        auto consumer = [](auto&&, channel<int[16]>& chan) -> fork<void, pool> {
            int val;
            // 等待数据
            while (!co_await(chan >> val)) {
                if (!chan.active()) break; // 如果非阻塞读取失败且通道已关闭，退出
                std::this_thread::yield();
            }
            EXPECT_EQ(val, 42);

            // 通道关闭后，读取应持续失败 (返回 false)
            // 需要通过 chan.active() 来区分是“暂时没数据”还是“永远没了”

            // 等待生产者关闭
            while (chan.active()) std::this_thread::yield();

            EXPECT_FALSE(co_await(chan >> val));
            }(co_await context(), chan);

        // 生产者
        auto producer = [](auto&&, channel<int[16]>& chan) -> fork<void, pool> {
            while (!co_await(chan << 42)); // 写入数据
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            chan.close();
            }(co_await context(), chan);

        co_await when_all(producer, consumer);
        }(env);

    test.join();
}

// --- 5. 无缓冲 Channel (Ty[]) 测试 (协程挂起) ---
TEST(ChannelTest, UnbufferedChannel) {
    auto env = make_environment(sche{});

    auto test = [](auto env) -> task<void, pool> {
        channel<int[]> chan; // 无缓冲，阻塞式

        // 生产者
        auto producer = [](auto&&, channel<int[]>& chan) -> fork<void, pool> {
            // 这是阻塞操作，必须等待消费者
            co_await(chan << 100);
            }(co_await context(), chan);

        // 消费者
        auto consumer = [](auto&&, channel<int[]>& chan) -> fork<void, pool> {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            int val;
            // 这是阻塞操作
            co_await(chan >> val);
            EXPECT_EQ(val, 100);
            }(co_await context(), chan);

        co_await when_all(producer, consumer);
        }(env);

    test.join();
}

*/