#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <sqlite3.h>
#include <vector>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <utility>
#include <iostream>
#include <thread>

#include "json.hpp"

using json = nlohmann::json;

std::string getCurrentDate(int offsetDays = 0) {
    auto now = std::chrono::system_clock::now();
    now += std::chrono::hours(24 * offsetDays);
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d");
    return ss.str();
}

std::string getCurrentHour() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%H");
    return ss.str();
}

void createFolder(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
        std::cout << "Directory created: " << path << std::endl;
    }
}

std::pair<std::string, std::string> getCurrentFolder() {
    int currentHour = std::stoi(getCurrentHour());
    std::string dateDir = (currentHour < 17) ? getCurrentDate(-1) : getCurrentDate();

    std::string fullDirPath = "/home/open/Inference-Log-Data/" + dateDir;
    std::string passengersDir = fullDirPath + "/passengers_data";

    createFolder(passengersDir);

    return {fullDirPath, ""};  // Only the first path is used
}

// Public inline function
inline std::string getPassengersDataFolder() {
    return getCurrentFolder().first + "/passengers_data";
}

std::string dbPath = getPassengersDataFolder() + "/passengers_data.db";

// Struct to hold metadata
struct PassengerData {
    // fetching for Passenger info
    int frameNumber;
    int personID;
    std::string timestamp;
    std::string personClass;
    std::string state;
    std::string imagePath;
    int onboardCount;
    int inCount;
    int outCount;
    double latitude;
    double longitude;
    double speed;
    std::string busID;
    int routeID;
    int uploadStatus;
};

// Callback for capturing response
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

// Function to fetch ALL rows from SQLite
std::vector<PassengerData> fetchAllPassengerData(const std::string& dbPath) {
    sqlite3* db;
    sqlite3_stmt* stmt;
    std::vector<PassengerData> passengers;

    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Failed to open DB: " << sqlite3_errmsg(db) << std::endl;
        return passengers;
    }

    const char* sql = "SELECT * FROM TrackingData ORDER BY FrameNumber";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare SQL statement: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return passengers;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PassengerData data;
        data.frameNumber = sqlite3_column_int(stmt, 0);
        data.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        data.personID = sqlite3_column_int(stmt, 2);
        data.personClass = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        data.state = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        data.inCount = sqlite3_column_int(stmt, 6);
        data.outCount = sqlite3_column_int(stmt, 7);
        data.onboardCount = sqlite3_column_int(stmt, 8);
        data.imagePath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        data.latitude = sqlite3_column_double(stmt, 9);
        data.longitude = sqlite3_column_double(stmt, 10);
        data.speed = sqlite3_column_double(stmt, 11);
        data.busID = getMacAddress("enP2p33s0");
        data.routeID = 0;
        data.uploadStatus = sqlite3_column_int(stmt, 12);
        passengers.push_back(data);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return passengers;
}

// Function to convert metadata to JSON string
std::string buildMetadataJson(const PassengerData& data) {
    std::ostringstream metadataStream;
    metadataStream << "{"
                   << "\"FrameNumber\": " << data.frameNumber << ", "
                   << "\"PersonID\": " << data.personID << ", "
                   << "\"PersonClass\": \"" << data.personClass << "\", "
                   << "\"State\": \"" << data.state << "\", "
                   << "\"ImagePath\": \"" << data.imagePath << "\", "
                   << "\"Latitude\": " << data.latitude << ", "
                   << "\"Longitude\": " << data.longitude << ", "
                   << "\"Speed\": " << data.speed << ", "
                   << "\"BusID\": \"" << data.busID << "\", "
                   << "\"RouteID\": \"" << data.routeID << "\", "
                   << "\"DeviceTimestamp\": \"" << data.timestamp << "\", "
                   << "\"OnboardCount\": " << data.onboardCount << ", "
                   << "\"OutCount\": " << data.outCount << ", "
                   << "\"InCount\": " << data.inCount << ", "
                   << "\"UploadStatus\": " << data.uploadStatus
                   << "}";
    return metadataStream.str();
}

bool updateUploadStatus(const std::string& dbPath, int frameNumber) {
    sqlite3* db;
    sqlite3_stmt* stmt;

    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Failed to open DB for update: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    const char* sql = "UPDATE TrackingData SET UploadStatus = 1 WHERE FrameNumber = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare update statement: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_int(stmt, 1, frameNumber);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to execute update statement: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return false;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    std::cout << "Upload status updated for FrameNumber: " << frameNumber << std::endl;
    return true;
}

// Function to send API request with metadata and image
bool sendToApi(const std::string& apiUrl, const std::string& apiKey, const std::string& imagePath, const std::string& metadataJson, int frameNumber){
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL.\n";
        return false;
    }

    curl_mime* mime = curl_mime_init(curl);
    curl_mimepart* part;

    // Add image
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "image");
    curl_mime_filedata(part, imagePath.c_str());
    curl_mime_type(part, "image/jpeg");

    std::cout << "Metadata JSON to be sent:\n" << metadataJson << std::endl; 

    // Add metadata
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "metadata");
    curl_mime_data(part, metadataJson.c_str(), CURL_ZERO_TERMINATED);
    curl_mime_type(part, "application/json");

    // Set URL and headers
    curl_easy_setopt(curl, CURLOPT_URL, apiUrl.c_str());
    struct curl_slist* headers = nullptr;
    std::string authHeader = "x-api-key: " + apiKey;
    headers = curl_slist_append(headers, authHeader.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Set MIME
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

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
            bool status = updateUploadStatus(dbPath, frameNumber);
            std::cout << "upload status : " << status << std::endl;
        }
        std::cout << "Response:\n" << response << "\n";
    }

    // Cleanup
    curl_easy_cleanup(curl);
    curl_mime_free(mime);
    curl_slist_free_all(headers);

    return res == CURLE_OK;
}

// Function to list network interfaces
int get_device_network_interfaces() {
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return 1;
    }

    std::cout << "Ethernet Interfaces:\n";

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr)
            continue;

#if defined(__linux__)
        if (ifa->ifa_addr->sa_family == AF_PACKET)
#elif defined(__APPLE__) || defined(__FreeBSD__)
        if (ifa->ifa_addr->sa_family == AF_LINK)
#endif
        {
            if (!(ifa->ifa_flags & IFF_LOOPBACK)) {
                std::cout << "- " << ifa->ifa_name << std::endl;
            }
        }
    }

    freeifaddrs(ifaddr);
    return 0;
}

bool isDeviceOnline() {
    int result = system("ping -c 1 -W 2 google.com > /dev/null 2>&1");
    return result == 0;
}

// Main function
int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    std::string apiUrlData = "https://apcsapi.airfi.in/upload";
    std::string apiKey = "b49a3d89-45d9-4c42-8c7f-6b2d0cf1c736";

    while (true) {
    	get_device_network_interfaces();
    	std::string mac = getMacAddress("enP2p33s0");  // Replace interface if needed

    	if (!mac.empty()) {
        	std::cout << "MAC Address: " << mac << std::endl;
    	} else {
        	std::cerr << "Failed to get MAC address" << std::endl;
    	}

	if (isDeviceOnline()) {
            
            std::cout << "Online: starting sync\n";
            
            // Fetch all passenger data
            std::vector<PassengerData> passengers = fetchAllPassengerData(dbPath);
	    std::cout << "Database path: " << dbPath << std::endl;

            std::string extractedImagePath, extractedPersonClass;
            int extractedFrameNumber, extractedUploadStatus=0;
	    int extractedOnboardCount, extractedPersonID;
	    int extractedInCount, extractedOutCount;

            if (passengers.empty()) {
                std::cerr << "No passenger data found in the database.\n";
            } else {
                for (const auto& data : passengers) {
                    std::string metadataJson = buildMetadataJson(data);
	            // Parse and extract
	            try {
            		json parsedJson = json::parse(metadataJson);
            		extractedImagePath = parsedJson["ImagePath"];
            		extractedFrameNumber = parsedJson["FrameNumber"];
            		extractedUploadStatus = parsedJson["UploadStatus"];
			extractedPersonClass = parsedJson["PersonClass"];
			extractedPersonID = parsedJson["PersonID"];
			extractedInCount = parsedJson["InCount"];
			extractedOutCount = parsedJson["OutCount"];
			extractedOnboardCount = parsedJson["OnboardCount"];
//                    	std::cout << "FrameNumber: " << extractedFrameNumber << " PersonID: " << extractedPersonID << " InCount: " << extractedInCount << " OutCount: " << extractedOutCount << " OnboardCount: " << extractedOnboardCount << " Upload Status: " << extractedUploadStatus << std::endl;

			if (extractedUploadStatus != 1 && extractedPersonClass != "kid") {            		
                    	  sendToApi(apiUrlData, apiKey, extractedImagePath, metadataJson, extractedFrameNumber);
                    	}
                    	else {
			std::cout << "FrameNumber: " << extractedFrameNumber << " PersonID: " << extractedPersonID << " InCount: " << extractedInCount << " OutCount: " << extractedOutCount << " OnboardCount: " << extractedOnboardCount << " Upload Status: " << extractedUploadStatus << std::endl;
                    	}
	            } catch (const std::exception& e) {
            		std::cerr << "Failed to parse metadataJson: " << e.what() << std::endl;
	            }
                }
            }
	} else {
		std::cerr << "Device is offline. Skipping sync.\n";
	}
	std::this_thread::sleep_for(std::chrono::seconds(10)); // Run loop every 10 seconds
    }
   
    curl_global_cleanup();
    return 0;
}
