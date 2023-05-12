#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace IOUnitTest
{
    class TestMachine
    {
    public:
        TestMachine();
        virtual ~TestMachine();

        class Line
        {
        public:
            double Timestamp;
            std::string Content;
            Line(double Timestamp, std::string Content);
        };

        class TestResult
        {
        public:
            std::vector<Line> Lines;
            std::string GetAllLines();
            std::string GetAllLinesDuring(double DurationStart, double DurationEnd);
        };

        TestResult Test(std::string Input, bool RestartProcess = true);
        void RunInCLI(bool RestartProcess = true);
    protected:
        int ReadInt(std::string Hint);
        long ReadLong(std::string Hint);
        float ReadFloat(std::string Hint);
        double ReadDouble(std::string Hint);
        std::string ReadWord(std::string Hint);
        std::string ReadLine(std::string Hint);

        void WriteLine(std::variant<int, long, float, double, char, std::string>);
        void WriteLine(std::initializer_list<std::variant<int, long, float, double, char, std::string>>);

        /// @brief The single-threaded function to be implemented that runs the test machine
        ///
        /// This function should always be blocked by a Read somewhere
        /// and provide output only using WriteLine.
        /// This function may be terminated any time.
        virtual void Process() = 0;
        virtual void OnProcessTermination() = 0;
    private:
        class TerminateException {};

        std::mutex Mutex;
        std::condition_variable ConditionVariable;
        /// NOTIFY ConditionVariable ON MODIFICATION
        /// SHOULD INCREASE ReadCount WHEN SETTING THIS (TWICE ON POPPING FROM ReadQueue)
        /// Set to true when there's no input to pop from ReadQueue
        bool WaitingToRead;
        /// NOTIFY ConditionVariable ON MODIFICATION
        int ReadCount;
        /// NOTIFY ConditionVariable ON MODIFICATION
        bool IsProcessRunning;
        /// NOTIFY ConditionVariable ON MODIFICATION
        /// Used on Read functions
        bool ShouldTerminate;
        bool IsTesting;

        std::string LastHint;
        /// Newlines should be inserted
        std::queue<char> ReadQueue;
        /// No newline at the end of the lines
        std::vector<Line> WriteQueue;

        std::thread ProcessThread;

        std::shared_mutex TimeMutex;
        std::chrono::time_point<std::chrono::steady_clock> StartTime;

        static std::string ToString(std::variant<int, long, float, double, char, std::string>);

        /// Locks the Mutex
        void Start(bool ClearInput = true);
        /// Locks the Mutex
        void Terminate();

        /// Doesn't lock the Mutex
        void ResetStartTime();
        /// Doesn't lock the Mutex
        double GetTime();

        /// Locks the Mutex
        void InputLine(std::string);
    };
}
