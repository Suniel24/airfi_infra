#include <iostream>
#include <sstream>
#include <string>
#include <cstdio>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <cstring>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <vector>
#include <iomanip>
#include <filesystem>
#include <utility>
#include <sqlite3.h>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

double g_latitude = 0.0;
double g_longitude = 0.0;
double g_speed_kmph = 0.0;
int g_satellites_available = 0;
int g_satellites_used = 0;
int gps_connected = 1;

// Mutex to protect global variable access
std::mutex data_mutex;

// Struct to hold metadata
struct DeviceData {
	std::string MacID;
	int CameraConnected;
	int OnBoardCount;
	int SatellitesCount;
	int SatellitesUsed;
	double Lat;
	double Lng;
	double Speed;
	int DeviceUpTime;
	int ServiceStatus;
	double Temperature;
	int GpsStatus;
	int uploadStatus;
};

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}

// Function to get MAC address of given interface
std::string getMacAddress(const std::string& interfaceName) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return "";
    }

    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, interfaceName.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    if (ioctl(fd, SIOCGIFHWADDR, &ifr) != 0) {
        perror("ioctl");
        close(fd);
        return "";
    }

    close(fd);

    unsigned char* mac = reinterpret_cast<unsigned char*>(ifr.ifr_hwaddr.sa_data);
    char macAddr[18];
    std::snprintf(macAddr, sizeof(macAddr), "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return std::string(macAddr);
}

int getCameraConnected() {
    std::ifstream file("/home/open/old_deploy/yolov8_multhreading_Airfi/install/rknn_yolov8_demo_Linux/airfi_data");
    if (!file.is_open()) {
        std::cerr << "Failed to open airfi_data file\n";
        return -1;
    }

    int first = 0, second = 0, third = 0;
    if (!(file >> first >> second >> third)) {
        std::cerr << "Failed to read three integers from file\n";
        return -1;
    }

    return third;  // Return onboard count
}


int getGpsStatus() { return gps_connected; }

int getServiceStatus() { 
	return system("ping -c 1 -W 2 google.com > /dev/null 2>&1") == 0 ? 1 : 0;
}

int getDeviceUpTime() {
    std::ifstream file("/proc/uptime");
    if (!file.is_open()) {
        return -1; // Error opening file
    }

    double uptimeSeconds;
    file >> uptimeSeconds;  // First value is the uptime in seconds
    return static_cast<int>(uptimeSeconds);
}

double getTemperature() {
	std::ifstream tempFile("/sys/class/thermal/thermal_zone0/temp");
	if (!tempFile.is_open()) {
		std::cerr << "Failed to open temperature file" << std::endl;
		return -1.0;
	}

	int tempMilliCelsius;
	tempFile >> tempMilliCelsius;

	// Convert from millidegrees Celsius to degrees Celsius
	return static_cast<double>(tempMilliCelsius) / 1000.0;
}

int getOnBoardCount() {
    std::ifstream file("/home/open/old_deploy/yolov8_multhreading_Airfi/install/rknn_yolov8_demo_Linux/airfi_data");

    if (!file.is_open()) {
        std::cerr << "Failed to open airfi_data file\n";
        return -1;
    }

    int first = 0, second = 0;
    if (!(file >> first >> second)) {
        std::cerr << "Failed to read two integers from file\n";
        return -1;
    }

    return first - second;
}

int getSatellitesCount() {
    std::lock_guard<std::mutex> lock(data_mutex);
    return g_satellites_available;
}

int getSatellitesUsed() {
    std::lock_guard<std::mutex> lock(data_mutex);
    return g_satellites_used;
}

double getLatitude() {
    std::lock_guard<std::mutex> lock(data_mutex);
    return g_latitude;
}

double getLongitude() {
    std::lock_guard<std::mutex> lock(data_mutex);
    return g_longitude;
}

double getSpeed() {
    std::lock_guard<std::mutex> lock(data_mutex);
    return g_speed_kmph;
}

void parseAndUpdateGlobals(const std::string& line) {
    try {
        auto j = json::parse(line);

	std::lock_guard<std::mutex> lock(data_mutex);

        if (j.contains("class") && j["class"] == "TPV") {
            g_latitude = j.value("lat", 0.0);
            g_longitude = j.value("lon", 0.0);
            double speed_mps = j.value("speed", 0.0);  // in m/s
            g_speed_kmph = speed_mps * 3.6;           // convert to km/h

        }
        if (j.contains("class") && j["class"] == "SKY") {
	    g_satellites_available = 0;
            g_satellites_used = 0;
            if (j.contains("satellites") && j["satellites"].is_array()) {
                for (const auto& sat : j["satellites"]) {
                    g_satellites_available++;
                    if (sat.value("used", false)) g_satellites_used++;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing JSON: " << e.what() << "\n";
    }
    if(g_satellites_used == 0)
	    gps_connected = 1;
    else
	    gps_connected = 2;
}

void readGpspipeOutput() {
    const char* cmd = "gpspipe -w -n 10";  // Limit to 10 lines for sampling
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        std::cerr << "Failed to open gpspipe\n";
        return;
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
        std::string line(buffer);
        parseAndUpdateGlobals(line);
    }
}

// Initialize local SQLite database and table
void initializeDatabase(const std::string& dbName) {
    sqlite3* db;
    char* errMsg = nullptr;
    int rc = sqlite3_open(dbName.c_str(), &db);

    std::string createTableSQL = R"(
        CREATE TABLE IF NOT EXISTS DeviceData (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            MacID TEXT,
            CameraConnected INTEGER,
            OnBoardCount INTEGER,
            SatellitesCount INTEGER,
            SatellitesUsed INTEGER,
            Lat REAL,
            Lng REAL,
            Speed REAL,
            DeviceUpTime INTEGER,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            ServiceStatus INTEGER,
            Temperature REAL,
	    GpsStatus INTEGER,
            uploadStatus INTEGER DEFAULT 0
        );
    )";

    rc = sqlite3_exec(db, createTableSQL.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }

    sqlite3_close(db);
}

// Insert current telemetry into SQLite
void insertDataIntoDB(const std::string& dbPath, const DeviceData& data) {
    sqlite3* db;
    sqlite3_stmt* stmt;
    int rc = sqlite3_open(dbPath.c_str(), &db);

    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    std::string insertSQL = R"(
        INSERT INTO DeviceData (
            MacID, CameraConnected, OnBoardCount, SatellitesCount, SatellitesUsed,
            Lat, Lng, Speed, DeviceUpTime, ServiceStatus, Temperature, GpsStatus, uploadStatus
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0);
    )";

    rc = sqlite3_prepare_v2(db, insertSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
	printf("i am here\n");
        std::cerr << "Prepare failed: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return;
    }

    sqlite3_bind_text(stmt, 1, getMacAddress("enP2p33s0").c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, getCameraConnected());
    sqlite3_bind_int(stmt, 3, getOnBoardCount());
    sqlite3_bind_int(stmt, 4, getSatellitesCount());
    sqlite3_bind_int(stmt, 5, getSatellitesUsed());
    sqlite3_bind_double(stmt, 6, getLatitude());
    sqlite3_bind_double(stmt, 7, getLongitude());
    sqlite3_bind_double(stmt, 8, getSpeed());
    sqlite3_bind_int(stmt, 9, getDeviceUpTime());
    sqlite3_bind_int(stmt, 10, getServiceStatus());
    sqlite3_bind_double(stmt, 11, getTemperature());
    sqlite3_bind_int(stmt, 12, getGpsStatus());

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Insert failed: " << sqlite3_errmsg(db) << std::endl;
    } else {
        std::cout << "Inserted into local DB." << std::endl;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

std::vector<DeviceData> fetchUnuploadedData(const std::string& dbPath) {
    std::vector<DeviceData> devicedata;
    sqlite3* db;
    sqlite3_stmt* stmt;
    sqlite3_open(dbPath.c_str(), &db);

    const char* sql = "SELECT * FROM DeviceData WHERE uploadStatus = 0";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            DeviceData data;
            data.MacID = (const char*)sqlite3_column_text(stmt, 1);
            data.CameraConnected = sqlite3_column_int(stmt, 2);
            data.OnBoardCount = sqlite3_column_int(stmt, 3);
            data.SatellitesCount = sqlite3_column_int(stmt, 4);
            data.SatellitesUsed = sqlite3_column_int(stmt, 5);
            data.Lat = sqlite3_column_double(stmt, 6);
            data.Lng = sqlite3_column_double(stmt, 7);
            data.Speed = sqlite3_column_double(stmt, 8);
            data.DeviceUpTime = sqlite3_column_int(stmt, 9);
            data.ServiceStatus = sqlite3_column_int(stmt, 11);
            data.Temperature = sqlite3_column_double(stmt, 12);
            data.GpsStatus = sqlite3_column_int(stmt, 13);
            data.uploadStatus = sqlite3_column_int(stmt, 14);
            devicedata.push_back(data);
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return devicedata;
}

bool updateUploadStatus(const std::string& dbPath, const std::string& macId) {
    sqlite3* db;
    sqlite3_open(dbPath.c_str(), &db);
    std::string updateSQL = "UPDATE DeviceData SET uploadStatus = 1 WHERE MacID = ? AND uploadStatus = 0";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, updateSQL.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, macId.c_str(), -1, SQLITE_TRANSIENT);
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return success;
}

// Function to convert metadata to JSON string
std::string buildMetadataJson(const DeviceData& data) {
    std::ostringstream metadataStream;
    metadataStream << "{"
                   << "\"MacID\": \"" << data.MacID << "\","
                   << "\"CameraConnected\": " << data.CameraConnected << ","
                   << "\"OnBoardCount\": " << data.OnBoardCount << ","
                   << "\"SatellitesCount\": " << data.SatellitesCount << ","
                   << "\"SatellitesUsed\": " << data.SatellitesUsed << ","
                   << "\"Lat\": " << data.Lat << ","
                   << "\"Lng\": " << data.Lng << ","
                   << "\"Speed\": " << data.Speed << ","
                   << "\"DeviceUpTime\": " << data.DeviceUpTime << ","
                   << "\"ServiceStatus\": " << data.ServiceStatus << ","
                   << "\"Temperature\": " << data.Temperature << ","
                   << "\"GpsStatus\": " << data.GpsStatus << ","
                   << "\"UploadStatus\": " << data.uploadStatus
                   << "}";

    return metadataStream.str();
}

// Function to send API request with metadata and image
bool sendToApi(const std::string& apiUrl, const std::string& apiKey, const std::string& metadataJson, const std::string& dbPath, const std::string& macId){
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL.\n";
        return false;
    }

    std::cout << "Metadata JSON to be sent:\n" << metadataJson << std::endl;

    // Set URL and headers
    curl_easy_setopt(curl, CURLOPT_URL, apiUrl.c_str());
    struct curl_slist* headers = nullptr;
    std::string authHeader = "x-api-key: " + apiKey;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, authHeader.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, metadataJson.c_str());	

    // Capture response
    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << "\n";
    } else {
        std::cout << "POST request sent successfully.\n";
        std::cout << "HTTP Status: " << http_code << "\n";
        if(http_code == 200){
            bool status = updateUploadStatus(dbPath, macId);
            std::cout << "upload status : " << status << std::endl;
	    }
        std::cout << "Response:\n" << response << "\n";
    }

    // Cleanup
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    return res == CURLE_OK;
}

bool isDeviceOnline() {
    int result = system("ping -c 1 -W 2 google.com > /dev/null 2>&1");
    return result == 0;
}

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    const std::string dbPath = "/home/open/old_deploy/yolov8_multhreading_Airfi/install/rknn_yolov8_demo_Linux/device_data.db";
    initializeDatabase(dbPath);

    std::string apiUrl = "https://apcsapi.airfi.in/device-data";
    std::string apiKey = "b49a3d89-45d9-4c42-8c7f-6b2d0cf1c736";

    while (true) {
	DeviceData newData;
        newData.MacID = getMacAddress("enP2p33s0");
        newData.CameraConnected = getCameraConnected();
        newData.OnBoardCount = getOnBoardCount();
        newData.SatellitesCount = getSatellitesCount();
        newData.SatellitesUsed = getSatellitesUsed();
        newData.Lat = getLatitude();
        newData.Lng = getLongitude();
        newData.Speed = getSpeed();
        newData.DeviceUpTime = getDeviceUpTime();
        newData.ServiceStatus = getServiceStatus();
        newData.Temperature = getTemperature();
        newData.GpsStatus = getGpsStatus();
        newData.uploadStatus = 0;

	insertDataIntoDB(dbPath, newData);

        if (isDeviceOnline()) {
	    std::vector<DeviceData> dataList = fetchUnuploadedData(dbPath);
            for (const auto& data : dataList) {
                if (data.uploadStatus != 1) {
                    std::string json = buildMetadataJson(data);
                    sendToApi(apiUrl, apiKey, json, dbPath, data.MacID);
                } else
			printf("skipped, already sent\n");
            }
        }
	readGpspipeOutput();
//        std::this_thread::sleep_for(std::chrono::minutes(3));
	std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    curl_global_cleanup();
    return 0;
}
