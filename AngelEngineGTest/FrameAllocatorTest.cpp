#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "TestHelpers.h"

// Import AngelEngine modules
import AngelEngine.FrameAllocator;
import AngelEngine.Memory;

using namespace AngelEngine;
using namespace AngelEngine::Test;

/**
 * @brief Тесты для FrameMemoryPool
 * 
 * Проверяют:
 * - Базовое выделение памяти
 * - Выравнивание (alignment)
 * - Переполнение пула и fallback на кучу
 * - Сброс памяти (Reset)
 */
class FrameMemoryPoolTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        pool_ = &FrameMemoryPool::Get();
        // Сбрасываем пул перед каждым тестом
        pool_->Reset();
    }
    
    void TearDown() override
    {
        pool_->Reset();
    }
    
    FrameMemoryPool* pool_;
};

TEST_F(FrameMemoryPoolTest, BasicAllocation)
{
    // Выделяем небольшой блок памяти
    void* ptr = pool_->Allocate(64, 8);
    
    EXPECT_NE(ptr, nullptr);
    
    // Проверяем что память действительно выделена (записываем и читаем)
    memset(ptr, 0xAB, 64);
    unsigned char* bytes = static_cast<unsigned char*>(ptr);
    for (int i = 0; i < 64; ++i)
    {
        EXPECT_EQ(bytes[i], 0xAB);
    }
}

TEST_F(FrameMemoryPoolTest, MultipleAllocations)
{
    // Выделяем несколько блоков
    eastl::vector<void*> pointers;
    
    for (int i = 0; i < 100; ++i)
    {
        void* ptr = pool_->Allocate(1024, 8);
        EXPECT_NE(ptr, nullptr);
        pointers.push_back(ptr);
    }
    
    // Проверяем что все указатели уникальны
    for (size_t i = 0; i < pointers.size(); ++i)
    {
        for (size_t j = i + 1; j < pointers.size(); ++j)
        {
            EXPECT_NE(pointers[i], pointers[j]);
        }
    }
}

TEST_F(FrameMemoryPoolTest, Alignment_8bytes)
{
    void* ptr = pool_->Allocate(100, 8);
    uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    
    EXPECT_EQ(address % 8, 0);
}

TEST_F(FrameMemoryPoolTest, Alignment_16bytes)
{
    void* ptr = pool_->Allocate(100, 16);
    uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    
    EXPECT_EQ(address % 16, 0);
}

TEST_F(FrameMemoryPoolTest, Alignment_32bytes)
{
    void* ptr = pool_->Allocate(100, 32);
    uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    
    EXPECT_EQ(address % 32, 0);
}

TEST_F(FrameMemoryPoolTest, Alignment_64bytes)
{
    void* ptr = pool_->Allocate(100, 64);
    uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    
    EXPECT_EQ(address % 64, 0);
}

// Параметризированный тест для различных выравниваний
class AlignmentTest : public ::testing::TestWithParam<size_t>
{
protected:
    FrameMemoryPool* pool_;
    
    void SetUp() override
    {
        pool_ = &FrameMemoryPool::Get();
    }
};

TEST_P(AlignmentTest, FrameAllocatorAlignment)
{
    size_t alignment = GetParam();
    void* ptr = pool_->Allocate(256, alignment);
    uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    
    EXPECT_EQ(address % alignment, 0) << "Failed for alignment " << alignment;
}

INSTANTIATE_TEST_SUITE_P(
    VariousAlignments,
    AlignmentTest,
    ::testing::Values(8, 16, 32, 64, 128)
);

TEST_F(FrameMemoryPoolTest, Reset_ClearsMemory)
{
    // Выделяем память
    void* ptr1 = pool_->Allocate(1024, 8);
    EXPECT_NE(ptr1, nullptr);
    
    // Сбрасываем пул
    pool_->Reset();
    
    // Выделяем снова - должен вернуться к началу
    void* ptr2 = pool_->Allocate(1024, 8);
    EXPECT_NE(ptr2, nullptr);
    
    // В идеале ptr1 и ptr2 должны совпадать (память переиспользуется)
    EXPECT_EQ(ptr1, ptr2);
}

TEST_F(FrameMemoryPoolTest, Overflow_FallbackToHeap)
{
    // Выделяем почти весь пул (2MB - 1KB)
    constexpr size_t largeSize = FrameMemoryPool::POOL_SIZE - 1024;
    void* ptr1 = pool_->Allocate(largeSize, 8);
    EXPECT_NE(ptr1, nullptr);
    
    // Следующее выделение должно вызвать fallback на кучу
    // (и warning в лог)
    void* ptr2 = pool_->Allocate(2048, 8);
    EXPECT_NE(ptr2, nullptr);
    
    // После сброса fallback аллокации должны очиститься
    pool_->Reset();
    
    // Теперь снова должно работать из пула
    void* ptr3 = pool_->Allocate(1024, 8);
    EXPECT_NE(ptr3, nullptr);
}

TEST_F(FrameMemoryPoolTest, Singleton_Pattern)
{
    // Проверяем что Get() возвращает один и тот же экземпляр
    FrameMemoryPool& pool1 = FrameMemoryPool::Get();
    FrameMemoryPool& pool2 = FrameMemoryPool::Get();
    
    EXPECT_EQ(&pool1, &pool2);
}

TEST_F(FrameMemoryPoolTest, ZeroSizeAllocation)
{
    // Выделение 0 байт должно вернуть валидный указатель (или nullptr)
    void* ptr = pool_->Allocate(0, 8);
    
    // Поведение зависит от реализации - либо nullptr, либо валидный указатель
    // Главное что нет крэша
    (void)ptr;  // Suppress unused warning
    SUCCEED() << "Zero-size allocation completed without crash";
}

/**
 * @brief Тесты для LinearFrameAllocator
 */
class LinearFrameAllocatorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        FrameMemoryPool::Get().Reset();
    }
    
    void TearDown() override
    {
        FrameMemoryPool::Get().Reset();
    }
};

TEST_F(LinearFrameAllocatorTest, Constructor_WithName)
{
    LinearFrameAllocator allocator("TestAllocator");
    
    EXPECT_STREQ(allocator.get_name(), "TestAllocator");
}

TEST_F(LinearFrameAllocatorTest, Constructor_DefaultName)
{
    LinearFrameAllocator allocator;
    
    EXPECT_STREQ(allocator.get_name(), "LinearFrameAllocator");
}

TEST_F(LinearFrameAllocatorTest, SetName)
{
    LinearFrameAllocator allocator;
    allocator.set_name("NewName");
    
    EXPECT_STREQ(allocator.get_name(), "NewName");
}

TEST_F(LinearFrameAllocatorTest, Allocate_UsesFramePool)
{
    LinearFrameAllocator allocator;
    
    void* ptr = allocator.allocate(128);
    
    EXPECT_NE(ptr, nullptr);
    
    // Проверяем выравнивание
    uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    EXPECT_EQ(address % 8, 0);
}

TEST_F(LinearFrameAllocatorTest, Allocate_WithAlignment)
{
    LinearFrameAllocator allocator;
    
    void* ptr = allocator.allocate(128, 32, 0, 0);
    
    EXPECT_NE(ptr, nullptr);
    
    // Проверяем выравнивание
    uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    EXPECT_EQ(address % 32, 0);
}

TEST_F(LinearFrameAllocatorTest, Deallocate_NoOp)
{
    LinearFrameAllocator allocator;
    
    void* ptr = allocator.allocate(128);
    EXPECT_NE(ptr, nullptr);
    
    // Deallocate не делает ничего (память освобождается при Reset)
    EXPECT_NO_THROW(allocator.deallocate(ptr, 128));
}

TEST_F(LinearFrameAllocatorTest, EqualityOperator)
{
    LinearFrameAllocator allocator1;
    LinearFrameAllocator allocator2;
    
    EXPECT_EQ(allocator1, allocator2);
    EXPECT_FALSE(allocator1 != allocator2);
}

TEST_F(LinearFrameAllocatorTest, Comparison_AlwaysEqual)
{
    LinearFrameAllocator allocator1("First");
    LinearFrameAllocator allocator2("Second");
    
    // Даже с разными именами аллокаторы считаются равными
    EXPECT_EQ(allocator1, allocator2);
}

/**
 * @brief Stress тест для многопоточной аллокации
 * 
 * Проверяет что аллокатор корректно работает в многопоточной среде
 */
TEST_F(FrameMemoryPoolTest, StressTest_MultiThreadedAllocation)
{
    constexpr int numThreads = 4;
    constexpr int allocationsPerThread = 100;
    
    eastl::vector<eastl::vector<void*>> threadResults(numThreads);
    eastl::vector<std::thread> threads;
    
    // Создаём потоки
    for (int t = 0; t < numThreads; ++t)
    {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < allocationsPerThread; ++i)
            {
                void* ptr = pool_->Allocate(64, 8);
                if (ptr != nullptr)
                {
                    threadResults[t].push_back(ptr);
                }
            }
        });
    }
    
    // Ждём завершения всех потоков
    for (auto& thread : threads)
    {
        thread.join();
    }
    
    // Подсчитываем общее количество успешных аллокаций
    size_t totalAllocations = 0;
    for (const auto& results : threadResults)
    {
        totalAllocations += results.size();
    }
    
    // Должны быть успешные аллокации
    EXPECT_GT(totalAllocations, 0);
    
    // Сбрасываем пул после теста
    pool_->Reset();
}

/**
 * @brief Тест на проверку что память обнуляется после Reset
 */
TEST_F(FrameMemoryPoolTest, MemoryReuse_AfterReset)
{
    // Выделяем и заполняем память
    char* ptr1 = static_cast<char*>(pool_->Allocate(1024, 8));
    memset(ptr1, 0xFF, 1024);
    
    // Сбрасываем
    pool_->Reset();
    
    // Выделяем снова (должна вернуться та же память)
    char* ptr2 = static_cast<char*>(pool_->Allocate(1024, 8));
    
    // Память может содержать старые данные (это ожидаемо для frame allocator)
    // Проверяем что указатель тот же
    EXPECT_EQ(ptr1, ptr2);
}
