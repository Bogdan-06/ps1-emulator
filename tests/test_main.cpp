#include <cstdlib>
#include <iostream>
#include <string_view>

void run_bus_tests();
void run_cli_tests();
void run_cpu_tests();
void run_disc_tests();
void run_display_tests();
void run_gpu_tests();

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

int main() {
    run_cli_tests();
    run_bus_tests();
    run_cpu_tests();
    run_disc_tests();
    run_display_tests();
    run_gpu_tests();
    std::cout << "All tests passed\n";
    return 0;
}
