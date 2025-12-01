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

using namespace coflux;

// --- 1. SPSC 基础测试 (非阻塞模式) ---
TEST(ChannelTest, BasicSPSC) {
    using pool = thread_pool_executor<>;
    using timer = timer_executor;
    using sche = scheduler<pool, timer>;
    auto env = make_environment(sche{});

    auto test = [](auto env) -> task<void, pool, sche> {
        channel<int[64]> chan; // 有界通道 (非阻塞)

        // 生产者
        auto producer = [](auto&&, channel<int[64]>& chan) -> coflux::fork<void, pool> {
            for (int i = 0; i < 100; ++i) {
                // 自旋重试直到写入成功
                while (!co_await(chan << i)) {}
            }
            }(co_await context(), chan);

        // 消费者
        auto consumer = [](auto&&, channel<int[64]>& chan) -> coflux::fork<void, pool> {
            int val;
            for (int i = 0; i < 100; ++i) {
                // 自旋重试直到读取成功
                while (!co_await(chan >> val)) {}
                EXPECT_EQ(val, i);
            }
            }(co_await context(), chan);

        co_await when_all(producer, consumer);
        }(env);

    test.join();
}

// --- 2. MPMC 并发测试 (非阻塞测试) ---
TEST(ChannelTest, ConcurrentMPMC) {
    using pool = thread_pool_executor<>;
    using timer = timer_executor;
    using sche = scheduler<pool, timer>;
    auto env = make_environment(sche{});

    auto test = [](auto env) -> task<void, pool, sche> {
        // 容量较小，强制触发满/空状态
        channel<int[16]> chan;
        const int N_PRODUCERS = 2;
        const int N_CONSUMERS = 2;
        const int ITEMS_PER_PRODUCER = 100;

        std::atomic<int> total_consumed = 0;

        // 生产者任务
        auto make_producer = [](auto&&, channel<int[16]>& chan, int id) -> coflux::fork<void, pool> {
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                int val = id * 100000 + i;
                // 遇到满队列则自旋重试
                while (!co_await(chan << val)) {
                    // co_await coflux::this_fork::yield();
                }
            }
            };

        // 消费者任务
        auto make_consumer = [](auto&&, channel<int[16]>& chan, std::atomic<int>& total_consumed) -> coflux::fork<void, pool> {
            int val;
            // 消费定额数据
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                // 遇到空队列则自旋重试
                while (!co_await(chan >> val)) {
					// std::cout << "SPIN in consumer\n";
                    // co_await coflux::this_fork::yield();
                }
                total_consumed.fetch_add(1, std::memory_order_relaxed);
            }
            };

        std::vector<coflux::fork<void, pool>> producers;
        std::vector<coflux::fork<void, pool>> consumers;

        auto&& ctx = co_await context();
        for (int i = 0; i < N_PRODUCERS; ++i) producers.push_back(make_producer(ctx, chan, i));
        for (int i = 0; i < N_CONSUMERS; ++i) consumers.push_back(make_consumer(ctx, chan, total_consumed));

        co_await when(producers);
        co_await when(consumers);
        EXPECT_EQ(total_consumed.load(), N_PRODUCERS * ITEMS_PER_PRODUCER);

        }(env);

    test.join();
}


// --- 3. 非阻塞语义测试  ---
TEST(ChannelTest, NonBlockingSemantics) {
    using pool = thread_pool_executor<>;
    using timer = timer_executor;
    using sche = scheduler<pool, timer>;
    auto env = make_environment(sche{});

    auto test = [](auto env) -> task<void, pool, sche> {
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

// --- 4. 无缓冲 Channel (Ty[]) 测试 (协程挂起) ---
TEST(ChannelTest, UnbufferedChannel) {
    using pool = thread_pool_executor<>;
    using timer = timer_executor;
    using sche = scheduler<pool, timer>;
    auto env = make_environment(sche{});

    auto test = [](auto env) -> task<void, pool, sche> {
        channel<int[]> chan; // 无缓冲，阻塞式

        // 生产者
        auto producer = [](auto&&, channel<int[]>& chan) -> coflux::fork<void, pool> {
            // 这是阻塞操作，必须等待消费者
            co_await(chan << 100);
            }(co_await context(), chan);

        // 消费者
        auto consumer = [](auto&&, channel<int[]>& chan) -> coflux::fork<void, pool> {
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

// --- 5. 关闭无缓冲通道测试(有缓冲通道无法手动关闭) ---
TEST(ChannelTest, CloseChannel) {
    using pool = thread_pool_executor<>;
    using timer = timer_executor;
    using sche = scheduler<pool, timer>;
    // 确保包含 timer 以支持 sleep_for
    auto env = make_environment(sche{});

    auto test = [](auto env) -> task<void, pool, sche> {
        channel<int[]> chan; // 无缓冲，握手同步

        // 消费者
        auto consumer = [](auto&&, channel<int[]>& chan) -> coflux::fork<void, pool> {
            int val;
            
            // 1. 正常读取
            // 无缓冲通道是“不见不散”的。
            // 如果生产者还没来，这里会【挂起】（Suspend），而不是返回 false。
            // 当生产者写入 42 时，这里会被唤醒并返回 true。
            EXPECT_TRUE(co_await(chan >> val));
            EXPECT_EQ(val, 42);

            // 2. 等待关闭
            // 此时生产者正在 sleep，然后会调用 close。
            // 我们再次尝试读取。因为没有生产者了，这里会再次【挂起】。
            // 当生产者调用 chan.close() 时，所有挂起的等待者会被唤醒，并返回 false。
            bool success = co_await(chan >> val);
            
            // 验证被 close 唤醒
            EXPECT_FALSE(success);
            EXPECT_FALSE(chan.active());
            
        }(co_await context(), chan);

        // 生产者
        auto producer = [](auto&&, channel<int[]>& chan) -> coflux::fork<void, pool> {
            // 1. 写入数据
            // 同样，如果消费者没准备好，这里也会挂起。
            EXPECT_TRUE(co_await(chan << 42));

            // 2. 模拟延迟
            // 让消费者有时间进入第二次 await (挂起状态)
            // 这样我们才能测试 close() 是否能正确唤醒它
            co_await coflux::this_fork::sleep_for(std::chrono::milliseconds(100));
            
            // 3. 关闭通道
            // 这应该触发 Clean()，唤醒所有在此通道上挂起的 reader/writer
            chan.close();
            
        }(co_await context(), chan);

        co_await when_all(producer, consumer);
    }(env);

    test.join();
}
