#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>

// File path
const std::string rs_filename = "/home/open/old_deploy/yolov8_multhreading_Airfi/install/rknn_yolov8_demo_Linux/airfi_data";
// Global variables
int rs_in = 0;
int rs_out = 0;
int rs_fg = 1;
// Function prototype
void rs_write_data_to_file();
// Function definition
void rs_write_data_to_file() {
    std::ofstream rs_file(rs_filename);
    if (!rs_file) {
        std::cerr << "Failed to open file for writing\n";
        exit(1);
    }
    rs_file << rs_in << std::endl;
    rs_file << rs_out << std::endl;
    rs_file << rs_fg << std::endl;
    rs_file.close();
    std::cout << "Data written to " << rs_filename << std::endl;
}
int main() {
    rs_write_data_to_file();
    return 0;
}
