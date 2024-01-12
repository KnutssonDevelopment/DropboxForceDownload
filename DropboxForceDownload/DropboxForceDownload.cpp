#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <thread>
#include <queue>
#include <stdexcept>
#include <mutex>
#include <condition_variable>

namespace fs = std::filesystem;
std::mutex console_mutex;

// Function to replace double backslashes with single ones in file paths for display.
std::string replaceAll(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

// Function to process individual files.
void processFile(const fs::path& file_path, bool debug) {
    if (file_path.empty()) {
        std::cerr << "Encountered an empty file path." << std::endl;
        return;
    }

    if (debug) {
        std::string path_str = file_path.string();
        path_str = replaceAll(path_str, "\\\\", "\\");
        std::lock_guard<std::mutex> guard(console_mutex);
        std::cout << "Downloading file: " << path_str << std::endl;
    }

    std::ifstream file(file_path, std::ios::binary);
    if (file.is_open()) {
        char buffer[1024]; // Read only the first 1 KB
        file.read(buffer, sizeof(buffer));
    }
    else {
        std::cerr << "Unable to open file: " << file_path << std::endl;
    }
}

// Recursive function to traverse directories and enqueue files for processing.
void traverseDirectory(const fs::path& directory_path, bool debug, std::queue<fs::path>& files, std::mutex& queue_mutex, std::condition_variable& cv) {
    for (const auto& entry : fs::directory_iterator(directory_path)) {
        if (fs::is_regular_file(entry.status())) {
            std::unique_lock<std::mutex> lock(queue_mutex);
            files.push(entry.path());
            cv.notify_one();
        }
        else if (fs::is_directory(entry.status())) {
            traverseDirectory(entry.path(), debug, files, queue_mutex, cv);
        }
    }
}

// Function to start directory traversal and manage threads.
void startDirectoryTraversal(const fs::path& directory_path, bool debug) {
    std::vector<std::thread> workers;
    std::queue<fs::path> files;
    std::mutex queue_mutex;
    std::condition_variable cv;
    bool done = false;

    // Worker function for threads to process files from the queue.
    auto worker = [&]() {
        while (true) {
            fs::path file_path;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                cv.wait(lock, [&] { return !files.empty() || done; });
                if (done && files.empty()) {
                    break;
                }
                file_path = files.front();
                files.pop();
            }
            processFile(file_path, debug);
        }
    };

    // Creating a pool of worker threads.
    int max_threads = std::thread::hardware_concurrency();
    if (debug) {
        std::cout << "Threads: " << max_threads << std::endl;
    }

    for (int i = 0; i < max_threads; ++i) {
        workers.emplace_back(worker);
    }

    // Starting the recursive directory traversal.
    traverseDirectory(directory_path, debug, files, queue_mutex, cv);

    // Signaling the workers that traversal is complete.
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        done = true;
        cv.notify_all();
    }

    // Joining all worker threads.
    for (auto& worker : workers) {
        worker.join();
    }
}

// Main function: Entry point of the program.
int main(int argc, char* argv[]) {
    std::locale::global(std::locale(""));
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <DropboxFolderPath> [debug]" << std::endl;
        return 1;
    }

    fs::path dropbox_path = argv[1];
    bool debug = (argc == 3 && std::string(argv[2]) == "debug");

    if (!fs::exists(dropbox_path) || !fs::is_directory(dropbox_path)) {
        std::cerr << "Invalid directory path: " << dropbox_path << std::endl;
        return 1;
    }

    try {
        startDirectoryTraversal(dropbox_path, debug);
    }
    catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << std::endl;
        return 1;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
