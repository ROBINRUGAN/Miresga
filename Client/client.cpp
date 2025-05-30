#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <random>
#include <chrono>
#include <curl/curl.h>
#include <algorithm>

const std::string base_url = "http://10.0.1.10/";
const std::vector<std::string> files = {
    "1KBFile", "8KBFile", "64KBFile", "512KBFile",
    "1MBFile", "8MBFile", "16MBFile", "100MBFile", "1GBFile"
};

std::mutex result_mutex;
std::vector<double> all_latencies;

std::string choose_file(std::mt19937 &rng) {
    std::uniform_int_distribution<> dist(0, files.size() - 1);
    return files[dist(rng)];
}

void perform_requests(int num_requests) {
    CURL *curl = curl_easy_init();
    if (!curl) return;

    std::random_device rd;
    std::mt19937 rng(rd());

    for (int i = 0; i < num_requests; ++i) {
        std::string url = base_url + choose_file(rng);
        auto start = std::chrono::high_resolution_clock::now();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char*, size_t size, size_t nmemb, void*) { return size * nmemb; });
        CURLcode res = curl_easy_perform(curl);
        auto end = std::chrono::high_resolution_clock::now();

        if (res == CURLE_OK) {
            double latency = std::chrono::duration<double, std::milli>(end - start).count();
            std::lock_guard<std::mutex> lock(result_mutex);
            all_latencies.push_back(latency);
        } else {
            std::cerr << "[Error] Request failed: " << curl_easy_strerror(res) << "\n";
        }
    }

    curl_easy_cleanup(curl);
}

void print_stats() {
    if (all_latencies.empty()) return;
    std::sort(all_latencies.begin(), all_latencies.end());
    double avg = std::accumulate(all_latencies.begin(), all_latencies.end(), 0.0) / all_latencies.size();
    double p95 = all_latencies[static_cast<size_t>(all_latencies.size() * 0.95)];
    std::cout << "Total requests: " << all_latencies.size() << "\n";
    std::cout << "Average latency: " << avg << " ms\n";
    std::cout << "P95 latency: " << p95 << " ms\n";
    std::cout << "Max latency: " << all_latencies.back() << " ms\n";
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: ./client <threads> <total_requests>\n";
        return 1;
    }

    int threads = std::stoi(argv[1]);
    int total_requests = std::stoi(argv[2]);
    int requests_per_thread = total_requests / threads;

    curl_global_init(CURL_GLOBAL_ALL);

    std::vector<std::thread> workers;
    for (int i = 0; i < threads; ++i) {
        workers.emplace_back(perform_requests, requests_per_thread);
    }

    for (auto &t : workers) t.join();

    curl_global_cleanup();
    print_stats();
    return 0;
} 