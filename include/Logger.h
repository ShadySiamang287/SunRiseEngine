#pragma once
#include <format>
#include <stacktrace>

#include <queue>
#include <semaphore>
#include <semaphore>
#include <mutex>
#include <atomic>
#include <thread>

#define ERROR_TEXT "\033[1;31m"
#define WARNING_TEXT "\033[33m"
#define LOG_TEXT "\033[34m"
#define DEFAULT_TEXT "\033[0m"
#define SUCCESS_TEXT "\033[32m"

namespace SUN{
    /// @brief A static logger class
    /// @details This logger runs on a separate thread using a non-busy wait and a queue. It supports format strings in the output, as well as warning colours
    class Logger{
    public:
        /// @brief The types and colours for output messages
        enum Severity{
            LOG, ///< <span style="color:#4fc3f7;">■</span> for general logging
            WARNING, ///< <span style="color:#fff176;">■</span> for warnings
            ERR, ///< <span style="color:#ef9a9a;">■</span> for errors
            SUCCESS, ///< <span style="color:#a5d6a7;">■</span> for success
        };

        /// @brief This intialises the logger thread
        static void Init(){
            running.store(true);
            outputThread = std::thread(WriterLoop);
        }

        /// @brief This shutdons the logger thread
        static void Shutdown(){
            running.store(false);
            logSemaphore.release();
            outputThread.join();
        }

        /// @brief Prints a log
        /// @tparam ...Args Allows the function to take in multiple arguments
        /// @param severity The severity of the warning 
        /// @param message The actual message, use {} to insert variables
        /// @param ...args The variables to be inserted. Can be 0
        template<typename... Args>
        static void Log(Severity severity, std::string_view message, Args... args){
            LogInternal(severity, message, false, std::forward<Args>(args)...);
        }
        
        /// @brief Prints a log with the file and line that called it
        /// @tparam ...Args Allows the function to take in multiple arguments
        /// @param severity The severity of the warning 
        /// @param message The actual message, use {} to insert variables
        /// @param ...args The variables to be inserted. Can be 0
        template<typename... Args>
        static void LogTrace(Severity severity, std::string_view message, Args... args){
            LogInternal(severity, message, true, std::forward<Args>(args)...);
        }

    private:
        static void Push(std::string&& msg){
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                logQueue.push(std::move(msg));
            }
            logSemaphore.release();
        }

        static void WriterLoop(){
            while (true){
                logSemaphore.acquire();

                while (true) {
                    std::string msg;
                    {
                        std::unique_lock<std::mutex> lock(queueMutex);
                        if (logQueue.empty()) break;
                        msg = std::move(logQueue.front());
                        logQueue.pop();
                    }
                    fwrite(msg.data(), 1, msg.size(), stderr);
                }

                if (!running.load() && logQueue.empty()){
                    break;
                }
            }
        }

        template<typename... Args>
        static void LogInternal(Severity severity, std::string_view message, bool trace, Args&&... args){
            std::string colour;
            std::string severityText;
            std::string traceString;

            if (trace){
                std::string file;
                int line = 0;

                std::stacktrace st = std::stacktrace::current();

                if (!st.empty()) {
                    std::stacktrace_entry top = st[2];  // the stackframe where the logger was called
                    if (!top.source_file().empty()) {
                        file =  top.source_file();
                        for (size_t i = file.length() - 1; i >= 0; i--){
                            //just get the name of the file
                            if (file[i] == '\\'){
                                file = file.substr(i + 1, file.length());
                                break;
                            }
                        }
                        line =  top.source_line();
                    }
                }
                if (!(file == "" && line == 0)){
                    traceString = std::format(", {}: {}", file, line);
                }
            }

            switch (severity){
            case Severity::ERR:
                colour = ERROR_TEXT;
                severityText = "ERROR";
                break;
            case Severity::WARNING:
                colour = WARNING_TEXT;
                severityText = "WARNING";
                break;
            case Severity::LOG:
                colour = LOG_TEXT;
                severityText = "LOG";
                break;
            case Severity::SUCCESS:
                colour = SUCCESS_TEXT;
                severityText = "SUCCESS";
                break;
            default:
                colour = DEFAULT_TEXT;
                severityText = "NONE";
                break;
            }
            // the preface text
            std::string type = std::format("[{}{}]: ", severityText, traceString);

            std::string output;
            try{
                // the actual output
                std::string msg = std::vformat(message, std::make_format_args(args...));
                output = std::vformat("{}{}{}{}\n", std::make_format_args(colour, type, msg, DEFAULT_TEXT));
            } catch  (const std::format_error& e){
                // if the args are wrong 
                std::string errorText(ERROR_TEXT);
                std::string defaultText(DEFAULT_TEXT);
                std::string whatText(e.what());
                output = std::vformat("{}[LOGGING ERROR] Invalid format string: {}{}\n", std::make_format_args(errorText, whatText, defaultText));
            }

            Push({std::move(output)});
        }

        inline static std::queue<std::string> logQueue;
        inline static std::mutex queueMutex;
        inline static std::counting_semaphore<1024> logSemaphore {0};
        inline static std::atomic<bool> running;
        inline static std::thread outputThread;
    };
}