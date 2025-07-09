#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <chrono>
#include <thread>
#include <cstdint>
#include <algorithm> // For std::max, std::min, std::search

#include <libusb-1.0/libusb.h>

#include <jpeglib.h>
#include <setjmp.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// --- Camera Configuration (Corrected based on capture_usb_packets_3s.cpp) ---
const uint16_t VENDOR_ID      = 0x0329;
const uint16_t PRODUCT_ID     = 0x2022;
const int INTERFACE_NUM       = 1;
const int ALT_SETTING         = 1;
const int BULK_EP_IN          = 0x81; // Correct IN endpoint
const int BULK_EP_OUT         = 0x01; // Command endpoint (PC to Camera)
const int MAX_PACKET_SIZE     = 512;
const double CAPTURE_DURATION_S  = 0.1;    // Capture duration in seconds
const char* RAW_FILENAME      = "image_data.raw";
const char* OUTPUT_FILENAME   = "output.png";
const char* EXTRACTED_JPEG_FILENAME = "extracted_frame.jpg"; // New filename for extracted JPEG

// --- Image Properties (from analysis) ---
// These are now hints, as JPEG will contain its own width/height
const int EXPECTED_IMAGE_WIDTH  = 640;
const int EXPECTED_IMAGE_HEIGHT = 480;

// --- Function Prototypes ---
bool capture_data();
bool convert_raw_to_image();
// // void find_all_jpeg_markers(const std::string& filename);


int main(int argc, char **argv) {
    if (argc > 1 && std::string(argv[1]) == "--convert-only") {
        if (!convert_raw_to_image()) {
            return 1;
        }
    } else {
        if (!capture_data()) {
            std::cerr << "Failed to capture data from camera." << std::endl;
            return 1;
        }

        // Find and print all JPEG markers for analysis
        // Find and print all JPEG markers for analysis
        // find_all_jpeg_markers(RAW_FILENAME);

        if (!convert_raw_to_image()) {
            std::cerr << "Failed to convert raw data to image." << std::endl;
            return 1;
        }
    }

    std::cout << "\nApplication finished successfully." << std::endl;
    return 0;
}

bool capture_data() {
    libusb_context *ctx = nullptr;
    libusb_device_handle *dev_handle = nullptr;
    int r;
    bool kernel_driver_detached = false; // Flag to track if kernel driver was detached

    r = libusb_init(&ctx);
    if (r < 0) {
        std::cerr << "Error initializing libusb: " << libusb_error_name(r) << std::endl;
        return false;
    }

    dev_handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!dev_handle) {
        std::cerr << "Could not find or open the USB device." << std::endl;
        libusb_exit(ctx);
        return false;
    }

    if (libusb_kernel_driver_active(dev_handle, INTERFACE_NUM) == 1) {
        if (libusb_detach_kernel_driver(dev_handle, INTERFACE_NUM) != 0) {
             std::cerr << "Could not detach kernel driver! Error: " << libusb_error_name(r) << std::endl;
             libusb_close(dev_handle);
             libusb_exit(ctx);
             return false;
        }
        kernel_driver_detached = true; // Set flag if detachment was successful
    }

    r = libusb_claim_interface(dev_handle, INTERFACE_NUM);
    if (r < 0) {
        std::cerr << "Error claiming interface: " << libusb_error_name(r) << std::endl;
        libusb_close(dev_handle);
        libusb_exit(ctx);
        return false;
    }

    r = libusb_set_interface_alt_setting(dev_handle, INTERFACE_NUM, ALT_SETTING);
    if (r != 0) {
        std::cerr << "Error setting alternate setting: " << libusb_error_name(r) << std::endl;
        libusb_release_interface(dev_handle, INTERFACE_NUM);
        libusb_close(dev_handle);
        libusb_exit(ctx);
        return false;
    }
    std::cout << "USB device configured." << std::endl;

    // --- Send Initialization Commands ---
    std::cout << "Sending start stream bulk command..." << std::endl;
    uint8_t start_cmd[] = {0xBB, 0xAA, 0x05, 0x00, 0x00};
    int transferred = 0;
    r = libusb_bulk_transfer(dev_handle, BULK_EP_OUT, start_cmd, sizeof(start_cmd), &transferred, 1000);
    if (r != 0 || transferred != sizeof(start_cmd)) {
        std::cerr << "Warning: Failed to send start command: " << libusb_error_name(r) << std::endl;
    }

    // --- Capture Data ---
    std::ofstream outfile(RAW_FILENAME, std::ios::binary);
    if (!outfile.is_open()) {
        std::cerr << "Error opening output file: " << RAW_FILENAME << std::endl;
        libusb_release_interface(dev_handle, INTERFACE_NUM);
        libusb_close(dev_handle);
        libusb_exit(ctx);
        return false;
    }

    auto start_time = std::chrono::steady_clock::now();
    std::cout << "Starting data capture for " << CAPTURE_DURATION_S << " seconds..." << std::endl;
    long long total_bytes = 0;

    while (true) {
        uint8_t buffer[MAX_PACKET_SIZE];
        int actual_length = 0;
        r = libusb_bulk_transfer(dev_handle, BULK_EP_IN, buffer, MAX_PACKET_SIZE, &actual_length, 1000); // Increased timeout

        if (r == 0 && actual_length > 0) {
            outfile.write(reinterpret_cast<char*>(buffer), actual_length);
            total_bytes += actual_length;
        } else if (r != LIBUSB_ERROR_TIMEOUT) {
            std::cerr << "Error reading from bulk endpoint: " << libusb_error_name(r) << std::endl;
            break;
        }

        auto current_time = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count() >= CAPTURE_DURATION_S) {
            std::cout << "Capture finished." << std::endl;
            break;
        }
    }

    outfile.close();
    std::cout << "Raw data saved to \"" << RAW_FILENAME << "\" (" << total_bytes << " bytes captured)." << std::endl;

    // --- Cleanup ---
    // Re-attach kernel driver if it was detached
    // if (libusb_kernel_driver_active(dev_handle, INTERFACE_NUM) == 0) { // Check if it's still detached
    //     if (libusb_attach_kernel_driver(dev_handle, INTERFACE_NUM) != 0) {
    //         std::cerr << "Warning: Could not re-attach kernel driver!" << std::endl;
    //     }
    // }

    std::cout << "Sending end stream bulk command..." << std::endl;
    uint8_t end_cmd[] = {0xBB, 0xAA, 0x06, 0x00, 0x00};
    int e_transferred = 0;
    r = libusb_bulk_transfer(dev_handle, BULK_EP_OUT, end_cmd, sizeof(end_cmd), &transferred, 1000);
    if (r != 0 || transferred != sizeof(start_cmd)) {
        std::cerr << "Warning: Failed to send end command: " << libusb_error_name(r) << std::endl;
    }

    libusb_release_interface(dev_handle, INTERFACE_NUM);
    libusb_close(dev_handle);
    libusb_exit(ctx);
    return true;
}

bool convert_raw_to_image() {
    // std::cout << "\n--- Image Conversion (JPEG/MJPEG) ---" << std::endl;
    std::ifstream raw_file(RAW_FILENAME, std::ios::binary);
    if (!raw_file.is_open()) {
        std::cerr << "Could not open raw data file: " << RAW_FILENAME << std::endl;
        return false;
    }

    // Read the entire raw file into memory
    std::vector<uint8_t> raw_data((std::istreambuf_iterator<char>(raw_file)), std::istreambuf_iterator<char>());
    raw_file.close();

    // Define proprietary packet header
    const std::vector<uint8_t> packet_header_start = {0xAA, 0xBB, 0x07};
    const size_t PACKET_HEADER_SIZE = 12; // Given in the problem description


    std::vector<uint8_t> concatenated_jpeg_payload;
    size_t current_pos = 0;

    while (current_pos < raw_data.size()) {
        auto header_it = std::search(raw_data.begin() + current_pos, raw_data.end(),
                                     packet_header_start.begin(), packet_header_start.end());

        if (header_it == raw_data.end()) {
            // No more packet headers found
            break;
        }

        // Advance current_pos past the found header start
        current_pos = std::distance(raw_data.begin(), header_it) + packet_header_start.size();

        // Ensure there's enough data for the full header
        if (current_pos + (PACKET_HEADER_SIZE - packet_header_start.size()) > raw_data.size()) {
            std::cerr << "Warning: Incomplete packet header found at end of raw data. Skipping." << std::endl;
            break;
        }

        // Skip the rest of the 12-byte header
        current_pos += (PACKET_HEADER_SIZE - packet_header_start.size());

        // Extract payload until the next packet header or end of raw_data
        auto next_header_it = std::search(raw_data.begin() + current_pos, raw_data.end(),
                                          packet_header_start.begin(), packet_header_start.end());

        size_t payload_end_pos;
        if (next_header_it == raw_data.end()) {
            payload_end_pos = raw_data.size();
        } else {
            payload_end_pos = std::distance(raw_data.begin(), next_header_it);
        }

        // Append payload data to concatenated_jpeg_payload
        concatenated_jpeg_payload.insert(concatenated_jpeg_payload.end(),
                                         raw_data.begin() + current_pos,
                                         raw_data.begin() + payload_end_pos);

        current_pos = payload_end_pos; // Move to the start of the next potential header
    }

    if (concatenated_jpeg_payload.empty()) {
        std::cerr << "Error: No valid JPEG payload data extracted from raw data." << std::endl;
        return false;
    }

    // Define JPEG markers as vectors
    const std::vector<uint8_t> jpeg_start_marker = {0xFF, 0xD8};
    const std::vector<uint8_t> jpeg_eoi_marker = {0xFF, 0xD9}; // End of Image

    std::vector<uint8_t> clean_jpeg_data;

    // Find the first FF D8 (SOI) in the concatenated payload
    auto start_it = std::search(concatenated_jpeg_payload.begin(), concatenated_jpeg_payload.end(),
                                jpeg_start_marker.begin(), jpeg_start_marker.end());
    if (start_it == concatenated_jpeg_payload.end()) {
        std::cerr << "Error: JPEG Start of Image (FF D8) marker not found in payload." << std::endl;
        return false;
    }

    // Find the first FF D9 (EOI) after the SOI
    auto end_it = std::search(start_it, concatenated_jpeg_payload.end(),
                              jpeg_eoi_marker.begin(), jpeg_eoi_marker.end());
    if (end_it == concatenated_jpeg_payload.end()) {
        std::cerr << "Error: JPEG End of Image (FF D9) marker not found after SOI in payload." << std::endl;
        return false;
    }

    // Copy the complete JPEG frame, including FF D8 and FF D9
    clean_jpeg_data.insert(clean_jpeg_data.end(), start_it, end_it + jpeg_eoi_marker.size());

    // Replace all occurrences of FF 24 with FF 00 to handle non-standard marker within scan data
    // This is kept as a precaution, as FF 24 might still appear within the actual JPEG data
    for (size_t i = 0; i + 1 < clean_jpeg_data.size(); ++i) {
        if (clean_jpeg_data[i] == 0xFF && clean_jpeg_data[i+1] == 0x24) {
            clean_jpeg_data[i+1] = 0x00; // Replace 0x24 with 0x00
        }
    }

    // --- Save extracted JPEG to file for external verification ---
    std::ofstream extracted_jpeg_outfile(EXTRACTED_JPEG_FILENAME, std::ios::binary);
    if (extracted_jpeg_outfile.is_open()) {
        extracted_jpeg_outfile.write(reinterpret_cast<const char*>(clean_jpeg_data.data()), clean_jpeg_data.size());
        extracted_jpeg_outfile.close();
        std::cout << "Cleaned JPEG frame saved to \"" << EXTRACTED_JPEG_FILENAME << "\" for analysis." << std::endl;
    } else {
        std::cerr << "Warning: Could not save cleaned JPEG to file." << std::endl;
    }

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jerr.trace_level = 0; // Set trace level to 2 for more detailed messages
    // Custom error handling for libjpeg-turbo
    jerr.error_exit = [](j_common_ptr cinfo) {
        (*cinfo->err->output_message)(cinfo);
        longjmp(*(jmp_buf*)cinfo->client_data, 1);
    };

    jmp_buf jpeg_jmp_buf;
    cinfo.client_data = (void*)&jpeg_jmp_buf;

    if (setjmp(jpeg_jmp_buf)) {
        jpeg_destroy_decompress(&cinfo);
        std::cerr << "Error: libjpeg-turbo failed to decompress JPEG data." << std::endl;
        return false;
    }

    jpeg_create_decompress(&cinfo);

    // Provide the JPEG data to the decompressor
    jpeg_mem_src(&cinfo, clean_jpeg_data.data(), clean_jpeg_data.size());

    // Read the JPEG header
    (void) jpeg_read_header(&cinfo, TRUE);

    // Set parameters for decompression
    cinfo.out_color_space = JCS_RGB; // Request RGB output
    cinfo.do_fancy_upsampling = TRUE; // Use fancy upsampling for better quality
    cinfo.do_block_smoothing = TRUE; // Use block smoothing

    // Start decompression
    (void) jpeg_start_decompress(&cinfo);

    int width = cinfo.output_width;
    int height = cinfo.output_height;
    int channels = cinfo.output_components;

    // Allocate output buffer
    unsigned char* img_data = new unsigned char[width * height * channels];
    JSAMPROW row_pointer[1];
    int row_stride = width * channels;

    // Read scanlines
    while (cinfo.output_scanline < cinfo.output_height) {
        row_pointer[0] = &img_data[cinfo.output_scanline * row_stride];
        (void) jpeg_read_scanlines(&cinfo, row_pointer, 1);
    }

    // Finish decompression
    (void) jpeg_finish_decompress(&cinfo);

    // Release JPEG decompression object
    jpeg_destroy_decompress(&cinfo);

    std::cout << "Decoded JPEG: " << width << "x" << height << " with " << channels << " channels." << std::endl;
    // std::cout << "Saving decoded image as PNG to \"" << OUTPUT_FILENAME << "\"" << std::endl;

    // int success = stbi_write_png(OUTPUT_FILENAME, width, height, 3, img_data, width * 3);

    delete[] img_data;

    // if (!success) {
    //     std::cerr << "Error saving PNG file." << std::endl;
    //     return false;
    // }

    // std::cout << "Image conversion successful! If the image is still corrupted, there might be other non-standard data or the JPEG stream itself is malformed in a way stb_image cannot handle." << std::endl;
    return true;
}

// /*
void find_all_jpeg_markers(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Debug: Could not open file for marker analysis: " << filename << std::endl;
        return;
    }

    std::vector<uint8_t> raw_data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    const std::vector<uint8_t> jpeg_start_marker = {0xFF, 0xD8};
    const std::vector<uint8_t> jpeg_end_marker = {0xFF, 0xD9};

    // std::cout << "\n--- JPEG Marker Analysis of " << filename << " ---" << std::endl;

    size_t offset = 0;
    auto current_it = raw_data.begin();

    while (current_it != raw_data.end()) {
        auto start_it = std::search(current_it, raw_data.end(), 
                                    jpeg_start_marker.begin(), jpeg_start_marker.end());
        
        if (start_it != raw_data.end()) {
            offset = std::distance(raw_data.begin(), start_it);
            // std::cout << "Found FF D8 (JPEG Start) at offset: 0x" << std::hex << offset << std::dec << std::endl;
            current_it = start_it + jpeg_start_marker.size();
        } else {
            break;
        }

        auto end_it = std::search(current_it, raw_data.end(), 
                                  jpeg_end_marker.begin(), jpeg_end_marker.end());
        
        if (end_it != raw_data.end()) {
            offset = std::distance(raw_data.begin(), end_it);
            std::cout << "Found FF D9 (JPEG End) at offset:   0x" << std::hex << offset << std::dec << std::endl;
            current_it = end_it + jpeg_end_marker.size();
        } else {
            // If FF D9 is not found after FF D8, it might be an incomplete frame or the last frame.
            std::cout << "Warning: FF D9 (JPEG End) not found after last FF D8." << std::endl;
            break;
        }
    }
    // std::cout << "------------------------------------\n" << std::endl;
}