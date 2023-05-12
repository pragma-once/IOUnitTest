#include "TestMachine.h"

#include <cctype>
#include <iostream>
#include <utility>

namespace IOUnitTest
{
    TestMachine::TestMachine() : WaitingToRead(false),
                                 ReadCount(0),
                                 IsProcessRunning(false),
                                 ShouldTerminate(false),
                                 IsTesting(false)
    {
        ResetStartTime();
        Start();
    }

    TestMachine::~TestMachine()
    {
        Terminate();
    }

    TestMachine::Line::Line(double Timestamp, std::string Content) : Timestamp(Timestamp), Content(Content)
    {
    }

    std::string TestMachine::TestResult::GetAllLines()
    {
        std::string result = "";
        for (auto line : Lines)
            result += line.Content + '\n';
        return result;
    }

    std::string TestMachine::TestResult::GetAllLinesDuring(double DurationStart, double DurationEnd)
    {
        std::string result = "";
        for (auto line : Lines)
            if (line.Timestamp >= DurationStart && line.Timestamp <= DurationEnd)
                result += line.Content + '\n';
        return result;
    }

    TestMachine::TestResult TestMachine::Test(std::string Input, bool RestartProcess)
    {
        std::unique_lock<std::mutex> guard(Mutex);
        if (IsTesting)
            throw std::logic_error("Cannot start another test while already testing.");
        IsTesting = true;

        if (!IsProcessRunning)
        {
            guard.unlock();
            Start();
            guard.lock();
        }
        else if (RestartProcess)
        {
            guard.unlock();
            Terminate();
            Start();
            guard.lock();
        } 

        ConditionVariable.wait(guard, [&]() {
            return WaitingToRead || !IsProcessRunning;
        });
        WriteQueue.clear();
        int c = ReadCount;

        guard.unlock();

        ResetStartTime();
        InputLine(Input);

        guard.lock();

        ConditionVariable.wait(guard, [&]() {
            return (WaitingToRead && c != ReadCount) || !IsProcessRunning;
        });

        TestResult result;
        result.Lines = std::exchange(WriteQueue, {});
        return result;
    }

    void TestMachine::RunInCLI(bool RestartProcess)
    {
        std::unique_lock<std::mutex> guard(Mutex);
        if (IsTesting)
            throw std::logic_error("Cannot start another test while already testing.");
        IsTesting = true;

        if (!IsProcessRunning)
        {
            guard.unlock();
            Start();
            guard.lock();
        }
        else if (RestartProcess)
        {
            guard.unlock();
            Terminate();
            Start();
            guard.lock();
        }

        ConditionVariable.wait(guard, [&]() {
            return WaitingToRead || !IsProcessRunning;
        });
        WriteQueue.clear();

        guard.unlock();

        ResetStartTime();

        guard.lock();
        int c = ReadCount - 1;
        while (IsProcessRunning)
        {
            ConditionVariable.wait(guard, [&]() {
                return (WaitingToRead && c != ReadCount) || !WriteQueue.empty() || !IsProcessRunning;
            });
            for (auto line : WriteQueue)
                std::cout << std::to_string(line.Timestamp) << ": " << line.Content << '\n';
            WriteQueue.clear();
            if (WaitingToRead && c != ReadCount)
            {
                c = ReadCount;
                std::cout << std::to_string(GetTime()) << ": " << LastHint << " > ";
                std::string input;
                std::getline(std::cin, input);
                guard.unlock();
                InputLine(input);
                guard.lock();
            }
        }
    }

    int TestMachine::ReadInt(std::string Hint)
    {
        std::string word = ReadWord(Hint);
        return std::stoi(word);
    }

    long TestMachine::ReadLong(std::string Hint)
    {
        std::string word = ReadWord(Hint);
        return std::stol(word);
    }

    float TestMachine::ReadFloat(std::string Hint)
    {
        std::string word = ReadWord(Hint);
        return std::stof(word);
    }

    double TestMachine::ReadDouble(std::string Hint)
    {
        std::string word = ReadWord(Hint);
        return std::stod(word);
    }

    std::string TestMachine::ReadWord(std::string Hint)
    {
        std::unique_lock<std::mutex> guard(Mutex);

        LastHint = Hint;

        while (std::isspace(ReadQueue.front()))
            ReadQueue.pop();

        while (ReadQueue.empty())
        {
            WaitingToRead = true;
            ReadCount++;
            guard.unlock();
            ConditionVariable.notify_all();
            guard.lock();

            ConditionVariable.wait(guard, [&]() {
                return !ReadQueue.empty() || ShouldTerminate;
            });

            WaitingToRead = false;
            ReadCount++;
            if (ShouldTerminate)
                throw TerminateException();
            guard.unlock();
            ConditionVariable.notify_all();
            guard.lock();

            while (std::isspace(ReadQueue.front()))
                ReadQueue.pop();
        }

        std::string result = "";
        while (!ReadQueue.empty())
        {
            char front = ReadQueue.front();
            if (std::isspace(front))
                break; // not popping it
            ReadQueue.pop();
            result += front;
        }

        return result;
    }

    std::string TestMachine::ReadLine(std::string Hint)
    {
        std::unique_lock<std::mutex> guard(Mutex);

        LastHint = Hint;

        if (ReadQueue.empty())
        {
            WaitingToRead = true;
            ReadCount++;
            guard.unlock();
            ConditionVariable.notify_all();
            guard.lock();

            ConditionVariable.wait(guard, [&]() {
                return !ReadQueue.empty() || ShouldTerminate;
            });

            WaitingToRead = false;
            ReadCount++;
            if (ShouldTerminate)
                throw TerminateException();
            guard.unlock();
            ConditionVariable.notify_all();
            guard.lock();
        }

        std::string result = "";
        while (!ReadQueue.empty())
        {
            char front = ReadQueue.front();
            ReadQueue.pop();
            if (front == '\n')
                break; // the newline isn't included, but is popped
            result += front;
        }

        return result;
    }

    void TestMachine::WriteLine(std::variant<int, long, float, double, char, std::string> Content)
    {
        std::unique_lock<std::mutex> guard(Mutex);
        Line line(GetTime(), ToString(Content) + '\n');
        WriteQueue.push_back(line);
        guard.unlock();
        ConditionVariable.notify_all();
    }

    void TestMachine::WriteLine(std::initializer_list<std::variant<int, long, float, double, char, std::string>> Contents)
    {
        std::unique_lock<std::mutex> guard(Mutex);
        Line line(GetTime(), "");
        for (auto content : Contents)
            line.Content += ToString(content);
        line.Content += '\n';
        WriteQueue.push_back(line);
        guard.unlock();
        ConditionVariable.notify_all();
    }

    std::string TestMachine::ToString(std::variant<int, long, float, double, char, std::string> Content)
    {
        if (std::holds_alternative<std::string>(Content))
            return std::get<std::string>(Content);
        if (std::holds_alternative<char>(Content))
            return std::to_string(std::get<char>(Content));
        if (std::holds_alternative<int>(Content))
            return std::to_string(std::get<int>(Content));
        if (std::holds_alternative<long>(Content))
            return std::to_string(std::get<long>(Content));
        if (std::holds_alternative<float>(Content))
            return std::to_string(std::get<float>(Content));
        if (std::holds_alternative<double>(Content))
            return std::to_string(std::get<double>(Content));
        return "";
    }

    void TestMachine::Start(bool ClearInput)
    {
        std::unique_lock<std::mutex> guard(Mutex);
        if (ClearInput)
            ReadQueue = {};
        if (IsProcessRunning)
        {
            guard.unlock();
            Terminate();
            guard.lock();
        }
        if (!IsProcessRunning) // Prevent starting twice
        {
            IsProcessRunning = true;
            ProcessThread = std::thread([&] {
                try
                {
                    Process();
                }
                catch(TerminateException&)
                {
                    OnProcessTermination();
                }

                std::unique_lock<std::mutex> guard(Mutex);
                IsProcessRunning = false;
                guard.unlock();
                ConditionVariable.notify_all();
            });
            guard.unlock();
            ConditionVariable.notify_all();
        }
    }

    void TestMachine::Terminate()
    {
        std::unique_lock<std::mutex> guard(Mutex);
        if (!IsProcessRunning       // Then no termination needed!
                || ShouldTerminate) // Prevent starting twice
            return;
        ShouldTerminate = true;
        guard.unlock();
        ConditionVariable.notify_all();
        ProcessThread.join();
    }

    void TestMachine::ResetStartTime()
    {
        std::unique_lock<std::shared_mutex> guard(TimeMutex);
        StartTime = std::chrono::steady_clock::now();
    }

    double TestMachine::GetTime()
    {
        std::unique_lock<std::shared_mutex> guard(TimeMutex);
        auto duration = std::chrono::steady_clock::now() - StartTime;
        return (double)std::chrono::duration_cast<std::chrono::microseconds>(duration).count() / 1000000.0;
    }

    void TestMachine::InputLine(std::string Line)
    {
        std::unique_lock<std::mutex> guard(Mutex);
        for (char character : Line)
            ReadQueue.push(character);
        ReadQueue.push('\n');
        guard.unlock();
        ConditionVariable.notify_all();
    }
}
